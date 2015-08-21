/*
 * This file is part of the GROMACS molecular simulation package.
 *
 * Copyright (c) 2011,2012,2013,2014,2015, by the GROMACS development team, led by
 * Mark Abraham, David van der Spoel, Berk Hess, and Erik Lindahl,
 * and including many others, as listed in the AUTHORS file in the
 * top-level source directory and at http://www.gromacs.org.
 *
 * GROMACS is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * GROMACS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with GROMACS; if not, see
 * http://www.gnu.org/licenses, or write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 *
 * If you want to redistribute modifications to GROMACS, please
 * consider that scientific software is very special. Version
 * control is crucial - bugs must be traceable. We will be happy to
 * consider code for inclusion in the official distribution, but
 * derived work must not be called official GROMACS. Details are found
 * in the README & COPYING files - if they are missing, get the
 * official version at http://www.gromacs.org.
 *
 * To help us fund GROMACS development, we humbly ask that you cite
 * the research papers on the package. Check out http://www.gromacs.org.
 */
/*! \internal \file
 * \brief
 * Implements classes and functions from refdata.h.
 *
 * \author Teemu Murtola <teemu.murtola@gmail.com>
 * \ingroup module_testutils
 */
#include "gmxpre.h"

#include "refdata.h"

#include <cstdlib>

#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "gromacs/options/basicoptions.h"
#include "gromacs/options/ioptionscontainer.h"
#include "gromacs/utility/exceptions.h"
#include "gromacs/utility/gmxassert.h"
#include "gromacs/utility/path.h"
#include "gromacs/utility/real.h"
#include "gromacs/utility/stringutil.h"

#include "testutils/refdata-checkers.h"
#include "testutils/refdata-impl.h"
#include "testutils/refdata-xml.h"
#include "testutils/testasserts.h"
#include "testutils/testexceptions.h"
#include "testutils/testfilemanager.h"

namespace gmx
{
namespace test
{

/********************************************************************
 * TestReferenceData::Impl declaration
 */

namespace internal
{

/*! \internal \brief
 * Private implementation class for TestReferenceData.
 *
 * \ingroup module_testutils
 */
class TestReferenceDataImpl
{
    public:
        //! Initializes a checker in the given mode.
        TestReferenceDataImpl(ReferenceDataMode mode, bool bSelfTestMode);

        //! Performs final reference data processing when test ends.
        void onTestEnd(bool testPassed);

        //! Full path of the reference data file.
        std::string             fullFilename_;
        /*! \brief
         * Root entry for comparing the reference data.
         *
         * Null after construction iff in compare mode and reference data was
         * not loaded successfully.
         * In all write modes, copies are present for nodes added to
         * \a outputRootEntry_, and ReferenceDataEntry::correspondingOutputEntry()
         * points to the copy in the output tree.
         */
        ReferenceDataEntry::EntryPointer  compareRootEntry_;
        /*! \brief
         * Root entry for writing new reference data.
         *
         * Null if only comparing against existing data.  Otherwise, starts
         * always as empty.
         * When creating new reference data, this is maintained as a copy of
         * \a compareRootEntry_.
         * When updating existing data, entries are added either by copying
         * from \a compareRootEntry_ (if they exist and comparison passes), or
         * by creating new ones.
         */
        ReferenceDataEntry::EntryPointer  outputRootEntry_;
        /*! \brief
         * Whether updating existing reference data.
         */
        bool                    updateMismatchingEntries_;
        //! `true` if self-testing (enables extra failure messages).
        bool                    bSelfTestMode_;
        /*! \brief
         * Whether any reference checkers have been created for this data.
         */
        bool                    bInUse_;
};

}       // namespace internal

/********************************************************************
 * Internal helpers
 */

namespace
{

//! Convenience typedef for a smart pointer to TestReferenceDataImpl.
typedef boost::shared_ptr<internal::TestReferenceDataImpl>
    TestReferenceDataImplPointer;

/*! \brief
 * Global reference data instance.
 *
 * The object is created when the test creates a TestReferenceData, and the
 * object is destructed (and other post-processing is done) at the end of each
 * test by ReferenceDataTestEventListener (which is installed as a Google Test
 * test listener).
 */
TestReferenceDataImplPointer g_referenceData;
//! Global reference data mode set with setReferenceDataMode().
// TODO: Make this a real enum (requires solving a TODO in EnumIntOption).
int                          g_referenceDataMode = erefdataCompare;

//! Returns the global reference data mode.
ReferenceDataMode getReferenceDataMode()
{
    return static_cast<ReferenceDataMode>(g_referenceDataMode);
}

//! Returns a reference to the global reference data object.
TestReferenceDataImplPointer initReferenceDataInstance()
{
    GMX_RELEASE_ASSERT(!g_referenceData,
                       "Test cannot create multiple TestReferenceData instances");
    g_referenceData.reset(new internal::TestReferenceDataImpl(getReferenceDataMode(), false));
    return g_referenceData;
}

//! Handles reference data creation for self-tests.
TestReferenceDataImplPointer initReferenceDataInstanceForSelfTest(ReferenceDataMode mode)
{
    if (g_referenceData)
    {
        GMX_RELEASE_ASSERT(g_referenceData.unique(),
                           "Test cannot create multiple TestReferenceData instances");
        g_referenceData->onTestEnd(true);
        g_referenceData.reset();
    }
    g_referenceData.reset(new internal::TestReferenceDataImpl(mode, true));
    return g_referenceData;
}

class ReferenceDataTestEventListener : public ::testing::EmptyTestEventListener
{
    public:
        virtual void OnTestEnd(const ::testing::TestInfo &test_info)
        {
            if (g_referenceData)
            {
                GMX_RELEASE_ASSERT(g_referenceData.unique(),
                                   "Test leaked TestRefeferenceData objects");
                g_referenceData->onTestEnd(test_info.result()->Passed());
                g_referenceData.reset();
            }
        }

        // Frees internal buffers allocated by libxml2.
        virtual void OnTestProgramEnd(const ::testing::UnitTest &)
        {
            cleanupReferenceData();
        }
};

}       // namespace

void initReferenceData(IOptionsContainer *options)
{
    // Needs to correspond to the enum order in refdata.h.
    const char *const refDataEnum[] =
    { "check", "create", "update-changed", "update-all" };
    options->addOption(
            EnumIntOption("ref-data")
                .enumValue(refDataEnum).store(&g_referenceDataMode)
                .description("Operation mode for tests that use reference data"));
    ::testing::UnitTest::GetInstance()->listeners().Append(
            new ReferenceDataTestEventListener);
}

/********************************************************************
 * TestReferenceDataImpl definition
 */

namespace internal
{

TestReferenceDataImpl::TestReferenceDataImpl(
        ReferenceDataMode mode, bool bSelfTestMode)
    : updateMismatchingEntries_(false), bSelfTestMode_(bSelfTestMode), bInUse_(false)
{
    const std::string dirname =
        bSelfTestMode
        ? TestFileManager::getGlobalOutputTempDirectory()
        : TestFileManager::getInputDataDirectory();
    const std::string filename = TestFileManager::getTestSpecificFileName(".xml");
    fullFilename_ = Path::join(dirname, "refdata", filename);

    switch (mode)
    {
        case erefdataCompare:
            if (File::exists(fullFilename_, File::throwOnError))
            {
                compareRootEntry_ = readReferenceDataFile(fullFilename_);
            }
            break;
        case erefdataCreateMissing:
            if (File::exists(fullFilename_, File::throwOnError))
            {
                compareRootEntry_ = readReferenceDataFile(fullFilename_);
            }
            else
            {
                compareRootEntry_ = ReferenceDataEntry::createRoot();
                outputRootEntry_  = ReferenceDataEntry::createRoot();
            }
            break;
        case erefdataUpdateChanged:
            if (File::exists(fullFilename_, File::throwOnError))
            {
                compareRootEntry_ = readReferenceDataFile(fullFilename_);
            }
            else
            {
                compareRootEntry_ = ReferenceDataEntry::createRoot();
            }
            outputRootEntry_          = ReferenceDataEntry::createRoot();
            updateMismatchingEntries_ = true;
            break;
        case erefdataUpdateAll:
            compareRootEntry_ = ReferenceDataEntry::createRoot();
            outputRootEntry_  = ReferenceDataEntry::createRoot();
            break;
    }
}

void TestReferenceDataImpl::onTestEnd(bool testPassed)
{
    // TODO: Only write the file with update-changed if there were actual changes.
    if (testPassed && bInUse_ && outputRootEntry_)
    {
        std::string dirname = Path::getParentPath(fullFilename_);
        if (!Directory::exists(dirname))
        {
            if (Directory::create(dirname) != 0)
            {
                GMX_THROW(TestException("Creation of reference data directory failed: " + dirname));
            }
        }
        writeReferenceDataFile(fullFilename_, *outputRootEntry_);
    }
}

}       // namespace internal


/********************************************************************
 * TestReferenceChecker::Impl
 */

/*! \internal \brief
 * Private implementation class for TestReferenceChecker.
 *
 * \ingroup module_testutils
 */
class TestReferenceChecker::Impl
{
    public:
        //! String constant for naming XML elements for boolean values.
        static const char * const    cBooleanNodeName;
        //! String constant for naming XML elements for string values.
        static const char * const    cStringNodeName;
        //! String constant for naming XML elements for integer values.
        static const char * const    cIntegerNodeName;
        //! String constant for naming XML elements for int64 values.
        static const char * const    cInt64NodeName;
        //! String constant for naming XML elements for unsigned int64 values.
        static const char * const    cUInt64NodeName;
        //! String constant for naming XML elements for floating-point values.
        static const char * const    cRealNodeName;
        //! String constant for naming XML attribute for value identifiers.
        static const char * const    cIdAttrName;
        //! String constant for naming compounds for vectors.
        static const char * const    cVectorType;
        //! String constant for naming compounds for sequences.
        static const char * const    cSequenceType;
        //! String constant for value identifier for sequence length.
        static const char * const    cSequenceLengthName;

        //! Creates a checker that does nothing.
        Impl();
        //! Creates a checker with a given root entry.
        Impl(const std::string &path, ReferenceDataEntry *compareRootEntry,
             ReferenceDataEntry *outputRootEntry, bool updateMismatchingEntries,
             bool bSelfTestMode, const FloatingPointTolerance &defaultTolerance);

        //! Returns the path of this checker with \p id appended.
        std::string appendPath(const char *id) const;

        //! Creates an entry with given parameters and fills it with \p checker.
        ReferenceDataEntry::EntryPointer
        createEntry(const char *type, const char *id,
                    const IReferenceDataEntryChecker &checker) const
        {
            ReferenceDataEntry::EntryPointer entry(new ReferenceDataEntry(type, id));
            checker.fillEntry(entry.get());
            return move(entry);
        }
        //! Checks an entry for correct type and using \p checker.
        ::testing::AssertionResult
        checkEntry(const ReferenceDataEntry &entry, const std::string &fullId,
                   const char *type, const IReferenceDataEntryChecker &checker) const
        {
            if (entry.type() != type)
            {
                return ::testing::AssertionFailure()
                       << "Mismatching reference data item type" << std::endl
                       << "  In item: " << fullId << std::endl
                       << "   Actual: " << type << std::endl
                       << "Reference: " << entry.type();
            }
            return checker.checkEntry(entry, fullId);
        }
        //! Finds an entry by id and updates the last found entry pointer.
        ReferenceDataEntry *findEntry(const char *id);
        /*! \brief
         * Finds/creates a reference data entry to match against.
         *
         * \param[in]  type   Type of entry to create.
         * \param[in]  id     Unique identifier of the entry (can be NULL, in
         *      which case the next entry without an id is matched).
         * \param[out] checker  Checker to use for filling out created entries.
         * \returns    Matching entry, or NULL if no matching entry found
         *      (NULL is never returned in write mode; new entries are created
         *      instead).
         */
        ReferenceDataEntry *
        findOrCreateEntry(const char *type, const char *id,
                          const IReferenceDataEntryChecker &checker);
        /*! \brief
         * Helper method for checking a reference data value.
         *
         * \param[in]  name   Type of entry to find.
         * \param[in]  id     Unique identifier of the entry (can be NULL, in
         *     which case the next entry without an id is matched).
         * \param[in]  checker  Checker that provides logic specific to the
         *     type of the entry.
         * \returns    Whether the reference data matched, including details
         *     of the mismatch if the comparison failed.
         * \throws     TestException if there is a problem parsing the
         *     reference data.
         *
         * Performs common tasks in checking a reference value, such as
         * finding or creating the correct entry.
         * Caller needs to provide a checker object that provides the string
         * value for a newly created entry and performs the actual comparison
         * against a found entry.
         */
        ::testing::AssertionResult
        processItem(const char *name, const char *id,
                    const IReferenceDataEntryChecker &checker);
        /*! \brief
         * Whether the checker should ignore all validation calls.
         *
         * This is used to ignore any calls within compounds for which
         * reference data could not be found, such that only one error is
         * issued for the missing compound, instead of every individual value.
         */
        bool shouldIgnore() const { return compareRootEntry_ == NULL; }

        //! Default floating-point comparison tolerance.
        FloatingPointTolerance  defaultTolerance_;
        /*! \brief
         * Human-readable path to the root node of this checker.
         *
         * For the root checker, this will be "/", and for each compound, the
         * id of the compound is added.  Used for reporting comparison
         * mismatches.
         */
        std::string             path_;
        /*! \brief
         * Current entry under which reference data is searched for comparison.
         *
         * Points to either the TestReferenceDataImpl::compareRootEntry_, or to
         * a compound entry in the tree rooted at that entry.
         *
         * Can be NULL, in which case this checker does nothing (doesn't even
         * report errors, see shouldIgnore()).
         */
        ReferenceDataEntry     *compareRootEntry_;
        /*! \brief
         * Current entry under which entries for writing are created.
         *
         * Points to either the TestReferenceDataImpl::outputRootEntry_, or to
         * a compound entry in the tree rooted at that entry.  NULL if only
         * comparing, or if shouldIgnore() returns `false`.
         */
        ReferenceDataEntry     *outputRootEntry_;
        /*! \brief
         * Iterator to a child of \a compareRootEntry_ that was last found.
         *
         * If `compareRootEntry_->isValidChild()` returns false, no entry has
         * been found yet.
         * After every check, is updated to point to the entry that was used
         * for the check.
         * Subsequent checks start the search for the matching node on this
         * node.
         */
        ReferenceDataEntry::ChildIterator lastFoundEntry_;
        /*! \brief
         * Whether the reference data is being written (true) or compared
         * (false).
         */
        bool                    updateMismatchingEntries_;
        //! `true` if self-testing (enables extra failure messages).
        bool                    bSelfTestMode_;
        /*! \brief
         * Current number of unnamed elements in a sequence.
         *
         * It is the index of the next added unnamed element.
         */
        int                     seqIndex_;
};

const char *const TestReferenceChecker::Impl::cBooleanNodeName    = "Bool";
const char *const TestReferenceChecker::Impl::cStringNodeName     = "String";
const char *const TestReferenceChecker::Impl::cIntegerNodeName    = "Int";
const char *const TestReferenceChecker::Impl::cInt64NodeName      = "Int64";
const char *const TestReferenceChecker::Impl::cUInt64NodeName     = "UInt64";
const char *const TestReferenceChecker::Impl::cRealNodeName       = "Real";
const char *const TestReferenceChecker::Impl::cIdAttrName         = "Name";
const char *const TestReferenceChecker::Impl::cVectorType         = "Vector";
const char *const TestReferenceChecker::Impl::cSequenceType       = "Sequence";
const char *const TestReferenceChecker::Impl::cSequenceLengthName = "Length";


TestReferenceChecker::Impl::Impl()
    : defaultTolerance_(defaultRealTolerance()),
      compareRootEntry_(NULL), outputRootEntry_(NULL),
      updateMismatchingEntries_(false), bSelfTestMode_(false), seqIndex_(0)
{
}


TestReferenceChecker::Impl::Impl(const std::string &path,
                                 ReferenceDataEntry *compareRootEntry,
                                 ReferenceDataEntry *outputRootEntry,
                                 bool updateMismatchingEntries, bool bSelfTestMode,
                                 const FloatingPointTolerance &defaultTolerance)
    : defaultTolerance_(defaultTolerance), path_(path + "/"),
      compareRootEntry_(compareRootEntry), outputRootEntry_(outputRootEntry),
      lastFoundEntry_(compareRootEntry->children().end()),
      updateMismatchingEntries_(updateMismatchingEntries),
      bSelfTestMode_(bSelfTestMode), seqIndex_(0)
{
}


std::string
TestReferenceChecker::Impl::appendPath(const char *id) const
{
    std::string printId = (id != NULL) ? id : formatString("[%d]", seqIndex_);
    return path_ + printId;
}


ReferenceDataEntry *TestReferenceChecker::Impl::findEntry(const char *id)
{
    ReferenceDataEntry::ChildIterator entry = compareRootEntry_->findChild(id, lastFoundEntry_);
    seqIndex_ = (id == NULL) ? seqIndex_+1 : 0;
    if (compareRootEntry_->isValidChild(entry))
    {
        lastFoundEntry_ = entry;
        return entry->get();
    }
    return NULL;
}

ReferenceDataEntry *
TestReferenceChecker::Impl::findOrCreateEntry(
        const char *type, const char *id,
        const IReferenceDataEntryChecker &checker)
{
    ReferenceDataEntry *entry = findEntry(id);
    if (entry == NULL && outputRootEntry_ != NULL)
    {
        lastFoundEntry_ = compareRootEntry_->addChild(createEntry(type, id, checker));
        entry           = lastFoundEntry_->get();
    }
    return entry;
}

::testing::AssertionResult
TestReferenceChecker::Impl::processItem(const char *type, const char *id,
                                        const IReferenceDataEntryChecker &checker)
{
    if (shouldIgnore())
    {
        return ::testing::AssertionSuccess();
    }
    std::string         fullId = appendPath(id);
    ReferenceDataEntry *entry  = findOrCreateEntry(type, id, checker);
    if (entry == NULL)
    {
        return ::testing::AssertionFailure()
               << "Reference data item " << fullId << " not found";
    }
    ::testing::AssertionResult result(checkEntry(*entry, fullId, type, checker));
    if (outputRootEntry_ != NULL && entry->correspondingOutputEntry() == NULL)
    {
        if (!updateMismatchingEntries_ || result)
        {
            outputRootEntry_->addChild(entry->cloneToOutputEntry());
        }
        else
        {
            ReferenceDataEntry::EntryPointer outputEntry(createEntry(type, id, checker));
            entry->setCorrespondingOutputEntry(outputEntry.get());
            outputRootEntry_->addChild(move(outputEntry));
            return ::testing::AssertionSuccess();
        }
    }
    if (bSelfTestMode_ && !result)
    {
        ReferenceDataEntry expected(type, id);
        checker.fillEntry(&expected);
        result << std::endl
        << "String value: " << expected.value() << std::endl
        << " Ref. string: " << entry->value();
    }
    return result;
}


/********************************************************************
 * TestReferenceData
 */

TestReferenceData::TestReferenceData()
    : impl_(initReferenceDataInstance())
{
}


TestReferenceData::TestReferenceData(ReferenceDataMode mode)
    : impl_(initReferenceDataInstanceForSelfTest(mode))
{
}


TestReferenceData::~TestReferenceData()
{
}


TestReferenceChecker TestReferenceData::rootChecker()
{
    if (!impl_->bInUse_ && !impl_->compareRootEntry_)
    {
        ADD_FAILURE() << "Reference data file not found: "
        << impl_->fullFilename_;
    }
    impl_->bInUse_ = true;
    if (!impl_->compareRootEntry_)
    {
        return TestReferenceChecker(new TestReferenceChecker::Impl());
    }
    return TestReferenceChecker(
            new TestReferenceChecker::Impl("", impl_->compareRootEntry_.get(),
                                           impl_->outputRootEntry_.get(),
                                           impl_->updateMismatchingEntries_, impl_->bSelfTestMode_,
                                           defaultRealTolerance()));
}


/********************************************************************
 * TestReferenceChecker
 */

TestReferenceChecker::TestReferenceChecker(Impl *impl)
    : impl_(impl)
{
}


TestReferenceChecker::TestReferenceChecker(const TestReferenceChecker &other)
    : impl_(new Impl(*other.impl_))
{
}


TestReferenceChecker &
TestReferenceChecker::operator=(const TestReferenceChecker &other)
{
    impl_.reset(new Impl(*other.impl_));
    return *this;
}


TestReferenceChecker::~TestReferenceChecker()
{
}


void TestReferenceChecker::setDefaultTolerance(
        const FloatingPointTolerance &tolerance)
{
    impl_->defaultTolerance_ = tolerance;
}


bool TestReferenceChecker::checkPresent(bool bPresent, const char *id)
{
    if (impl_->shouldIgnore() || impl_->outputRootEntry_ != NULL)
    {
        return bPresent;
    }
    ReferenceDataEntry::ChildIterator  entry
        = impl_->compareRootEntry_->findChild(id, impl_->lastFoundEntry_);
    const bool                         bFound
        = impl_->compareRootEntry_->isValidChild(entry);
    if (bFound != bPresent)
    {
        ADD_FAILURE() << "Mismatch while checking reference data item '"
        << impl_->appendPath(id) << "'\n"
        << "Expected: " << (bPresent ? "it is present.\n" : "it is absent.\n")
        << "  Actual: " << (bFound ? "it is present." : "it is absent.");
    }
    if (bFound && bPresent)
    {
        impl_->lastFoundEntry_ = entry;
        return true;
    }
    return false;
}


TestReferenceChecker TestReferenceChecker::checkCompound(const char *type, const char *id)
{
    if (impl_->shouldIgnore())
    {
        return TestReferenceChecker(new Impl());
    }
    std::string         fullId = impl_->appendPath(id);
    NullChecker         checker;
    ReferenceDataEntry *entry  = impl_->findOrCreateEntry(type, id, checker);
    if (entry == NULL)
    {
        ADD_FAILURE() << "Reference data item " << fullId << " not found";
        return TestReferenceChecker(new Impl());
    }
    if (impl_->updateMismatchingEntries_)
    {
        entry->makeCompound(type);
    }
    else
    {
        ::testing::AssertionResult result(impl_->checkEntry(*entry, fullId, type, checker));
        EXPECT_PLAIN(result);
        if (!result)
        {
            return TestReferenceChecker(new Impl());
        }
    }
    if (impl_->outputRootEntry_ != NULL && entry->correspondingOutputEntry() == NULL)
    {
        impl_->outputRootEntry_->addChild(entry->cloneToOutputEntry());
    }
    return TestReferenceChecker(
            new Impl(fullId, entry, entry->correspondingOutputEntry(),
                     impl_->updateMismatchingEntries_, impl_->bSelfTestMode_,
                     impl_->defaultTolerance_));
}


void TestReferenceChecker::checkBoolean(bool value, const char *id)
{
    EXPECT_PLAIN(impl_->processItem(Impl::cBooleanNodeName, id,
                                    ExactStringChecker(value ? "true" : "false")));
}


void TestReferenceChecker::checkString(const char *value, const char *id)
{
    EXPECT_PLAIN(impl_->processItem(Impl::cStringNodeName, id,
                                    ExactStringChecker(value)));
}


void TestReferenceChecker::checkString(const std::string &value, const char *id)
{
    EXPECT_PLAIN(impl_->processItem(Impl::cStringNodeName, id,
                                    ExactStringChecker(value)));
}


void TestReferenceChecker::checkTextBlock(const std::string &value,
                                          const char        *id)
{
    EXPECT_PLAIN(impl_->processItem(Impl::cStringNodeName, id,
                                    ExactStringBlockChecker(value)));
}


void TestReferenceChecker::checkInteger(int value, const char *id)
{
    EXPECT_PLAIN(impl_->processItem(Impl::cIntegerNodeName, id,
                                    ExactStringChecker(formatString("%d", value))));
}

void TestReferenceChecker::checkInt64(gmx_int64_t value, const char *id)
{
    EXPECT_PLAIN(impl_->processItem(Impl::cInt64NodeName, id,
                                    ExactStringChecker(formatString("%" GMX_PRId64, value))));
}

void TestReferenceChecker::checkUInt64(gmx_uint64_t value, const char *id)
{
    EXPECT_PLAIN(impl_->processItem(Impl::cUInt64NodeName, id,
                                    ExactStringChecker(formatString("%" GMX_PRIu64, value))));
}

void TestReferenceChecker::checkDouble(double value, const char *id)
{
    FloatingPointChecker<double> checker(value, impl_->defaultTolerance_);
    EXPECT_PLAIN(impl_->processItem(Impl::cRealNodeName, id, checker));
}


void TestReferenceChecker::checkFloat(float value, const char *id)
{
    FloatingPointChecker<float> checker(value, impl_->defaultTolerance_);
    EXPECT_PLAIN(impl_->processItem(Impl::cRealNodeName, id, checker));
}


void TestReferenceChecker::checkReal(float value, const char *id)
{
    checkFloat(value, id);
}


void TestReferenceChecker::checkReal(double value, const char *id)
{
    checkDouble(value, id);
}


void TestReferenceChecker::checkRealFromString(const std::string &value, const char *id)
{
    FloatingPointFromStringChecker<real> checker(value, impl_->defaultTolerance_);
    EXPECT_PLAIN(impl_->processItem(Impl::cRealNodeName, id, checker));
}


void TestReferenceChecker::checkVector(const int value[3], const char *id)
{
    TestReferenceChecker compound(checkCompound(Impl::cVectorType, id));
    compound.checkInteger(value[0], "X");
    compound.checkInteger(value[1], "Y");
    compound.checkInteger(value[2], "Z");
}


void TestReferenceChecker::checkVector(const float value[3], const char *id)
{
    TestReferenceChecker compound(checkCompound(Impl::cVectorType, id));
    compound.checkReal(value[0], "X");
    compound.checkReal(value[1], "Y");
    compound.checkReal(value[2], "Z");
}


void TestReferenceChecker::checkVector(const double value[3], const char *id)
{
    TestReferenceChecker compound(checkCompound(Impl::cVectorType, id));
    compound.checkReal(value[0], "X");
    compound.checkReal(value[1], "Y");
    compound.checkReal(value[2], "Z");
}


TestReferenceChecker
TestReferenceChecker::checkSequenceCompound(const char *id, size_t length)
{
    TestReferenceChecker compound(checkCompound(Impl::cSequenceType, id));
    compound.checkInteger(static_cast<int>(length), Impl::cSequenceLengthName);
    return compound;
}

} // namespace test
} // namespace gmx
