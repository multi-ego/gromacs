/*
 *       $Id$
 *
 *       This source code is part of
 *
 *        G   R   O   M   A   C   S
 *
 * GROningen MAchine for Chemical Simulations
 *
 *            VERSION 2.0
 * 
 * Copyright (c) 1991-1997
 * BIOSON Research Institute, Dept. of Biophysical Chemistry
 * University of Groningen, The Netherlands
 * 
 * Please refer to:
 * GROMACS: A message-passing parallel molecular dynamics implementation
 * H.J.C. Berendsen, D. van der Spoel and R. van Drunen
 * Comp. Phys. Comm. 91, 43-56 (1995)
 *
 * Also check out our WWW page:
 * http://rugmd0.chem.rug.nl/~gmx
 * or e-mail to:
 * gromacs@chem.rug.nl
 *
 * And Hey:
 * GRowing Old MAkes el Chrono Sweat
 */

#ifndef _g_hbond_cc
#define _g_hbond_cc

static char *SRCID_g_hbond_cc = "$Id$";

#ifdef HAVE_IDENT
#ident	"@(#) g_hbond.cc 1.29 9/30/97"
#endif /* HAVE_IDENT */

#include "copyrite.h"
#include "parse.h"
#include "dah.h"
#include "list.h"
#include "sysstuff.h"
#include "futil.h"
#include "physics.h"
#include "macros.h"
#include <fatal.h>

matrix box;       
rvec   *x;        
real   rcut   = 0.35;      
real   rcut2;
real   alfcut = 60;      
t_topology *top;
int    nr_hbonds;  
real   this_time; 
int    nr_frames; 
int    this_frame;
int    natoms=0;
t_mode mode;

int main(int argc,char *argv[])
{
  static char *desc[] = {
    "g_hbond is a program which computes hydrogen bonds from a ",
    "trajectroy file (trj), run input file (tpx), and index ", 
    "file (ndx). Hydrogen bonds are determined based on a cutoff ",
    "angle for the angle Hydrogen - Donor - Acceptor (zero is optimum)",
    "and a cutoff distance for the distance Donor - Acceptor.[PAR]",
    "[BB]Input:[bb][BR] ",
    "For the analysis you can specify one group of atoms. Then only ",
    "the hydrogen bonds inside this group are monitored. You can also ",
    "specify two or more groups. In that case g_hbond only computes ",
    "the hydrogen bonds between these two ( or more ) groups. This is ",
    "for instance usefull to calculate the hydrogen bonding between ",
    "protein and solvent ",
    "[PAR]It is also possible to analyse specific hydrogen bonds. Your ",
    "index file must then contain a group of formatted hydrogen bonds, ",
    "in the following way: ",
    "[PAR][TT]   1   9 [BR] ",
    "selected 9[BR] ",
    "    20    21    24[BR] ",
    "    25    26    29[BR] ",
    "     0     3     6[tt][BR][BR] ",
    "The selected group consists of triples of atom numbers i.e. Donor, ",
    "Hydrogen and Acceptor. Specifying a hydrogen bond. ",  
    "[PAR] It is also possible to compute solvent insertion into specific ",
    "hydrogen bonds. The index file then consists of a group of formatted ",
    "hydrogen bonds of which we want to calculate solvent insertion, and a ",
    "group of solvent ",
    "[PAR][BB]Output:[bb][BR] ",
    "The following files are generated by g_hbond:",
    "[PAR][TT]angle_inter.xvg,angle_internal.xvg,angle_total[tt][BR]",
    "These files contain a frequency distribution of all hydrogen bond ",
    "angles for all intermolecular, intramolecular, and ",
    "all hydrogenbonds respectively",
    "[PAR][TT]distance_inter.xvg,distance_internal.xvg,distance_total.xvg[tt][BR]",
    "These files contain a frequency distribution of all hydrogen bond ",
    "distances for all intermolecular, intramolecular, and ",
    "all hydrogenbonds respectively ",
    "[PAR][TT]number_inter.xvg,number_internal.xvg,number_total.xvg[tt][BR]",
    "These files contain the number of hydrogen bonds as a function of time ",
    "for all intermolecular, intramolecular, and ",
    "all hydrogenbonds respectively.",
    "[PAR][TT]hbmap_inter,hbmap_intra,hbmap_total[tt][BR]",
    "The hbmap files contain a matrix with the dimensions [TT]total number ",
    "of frames X total number of hydrogen bonds[tt], If hydrogen bond [IT]i[it] ",
    "exists at time frame [IT]j[it]. Then element [IT]ij[it] in the matrix ", 
    "is [TT]1[tt]. If the hydrogen bond does not exist then element [IT]ij[it] is ",
    "[TT]0[tt]. These [TT]hbmap_inter, hbmap_intra and hbmap_all[tt] files ",
    "represent the matrices for the intermolecular, intramolecular and all ",
    "hydrogen bonds. These hbmap files can be used to calculate the average ",
    "lifetime of hydrogen bonds. The lifetime is calculated by the program ",
    "[TT]g_lifetime[tt].",
    "[PAR][TT]n-n+3.xvg,n-n+4.xvg,n-n+5.xvg,helical.xvg[tt][BR]",
    "These files contain the number of hydrogen bonds as a function of time, ",
    "inside a molecule ( e.g. protein ) spaced 3, 4 or 5 residues. The file ",
    "[TT]helical.xvg[tt] contains the summation of [TT]n-n+3.xvg[tt],",
    "[TT]n-n+4.xvg[tt],[TT]n-n+5.xvg[tt] as a function of time.",
    "[PAR][TT]hydrogen_bonds.ndx[tt][BR]",
    "The [TT]hydrogen_bonds.ndx[tt] file is an index file of all found hydrogen ",
    "bonds. The file is split up into three groups: internal, intermolecular, and ",
    "all_bonds representing hydrogen bonds inside a molecule, between molecules, ",
    "and all bonds respectively.",
    "[PAR][TT]hbond.out[tt][BR]",
    "In the hbond.out file all hydrogen bonds are printed with the full names ",
    "atom numbers etc. of donor, hydrogen and acceptor. In this file also the ",
    "first occurence, the last occurence, and the number of frames of the ",
    "hydrogen bond is plotted.",
    "[PAR][TT]selected_n.xvg[tt][BR]",
    "These files are produced only when analysing selected hydrogen bonds. The ",
    "[TT]selected_n.xvg[tt] file contains the distance between donor and ",
    "acceptor as a function of time for hydrogen bond number n. This number ",
    "refers to the nth hydrogen bond in your input ndx file.",
    "[PAR][TT]matrix[tt][BR]",
    "This file is only generated when analysing selected or inserted hydrogen ",
    "bonds. This file is comparable to the hbmap files. ",
    "The time is printed in the first column. The next column contains a matrix",
    "filled with space (' '), pipe ('|') , minus('-') or plus ('+') symbols. ",
    "These symbols represent:[BR]",
    "[TT]space - no hydrogen bond and no inserted hydrogen bond[tt][BR]",
    "[TT]pipe  - hydrogen bond exists, but no inserted hydrogen bond[tt][BR]",
    "[TT]minus - no hydrogen bond, but inserted hydrogen bond exists[tt][BR]",
    "[TT]plus  - hydrogen bond exists, and inserted hydrogen bond exists[tt][BR]",
    "[PAR][TT]insert_n.xvg[tt][BR]",
    "This file is only generated when analysing inserted hydrogen bonds.",
    "This file contains the following data for hydrogen bond [TT]n[tt]:[BR]",
    "[TT] Col. Description[tt][BR]",
    "[TT] 1    Time[tt][BR]",
    "[TT] 2    Distance between donor and acceptor (nm)[tt][BR]",
    "[TT] 3    Distance between donor and nearest solvent atom[tt][BR]",
    "[TT] 4    Distance between acceptor and nearest solvent atom[tt][BR]",
    "[TT] 5    Atom number of nearest solvent atom[tt][BR]"
    };
  
  /* options */
  t_pargs pa [] = {
    { "-a", FALSE, etREAL, &alfcut,
      "cutoff angle (degrees, Hydrogen - Donor - Acceptor)" },
    { "-r", FALSE, etREAL, &rcut,
      "cutoff radius (nm, Donor - Acceptor)" }
  }; 
  
  Hbond      **dah=NULL;
  int        nr_dah,i;
  int        status;
  List       list;


  t_filenm fnm[] = {
    { efTRX, "-f",   NULL,    ffREAD },
    { efNDX, NULL,   NULL,    ffREAD },
    { efTPX, NULL,   NULL,    ffREAD },
    { efOUT, "-o","hbond",    ffWRITE }
  };
#define NFILE asize(fnm)

  /* copyright */
  CopyRight(stderr,argv[0]);

  /* parse arguments and read user choices */  
  parse_common_args(&argc,argv,PCA_CAN_TIME,TRUE,NFILE,fnm,asize(pa),pa,
		    asize(desc),desc,0,NULL);
  rcut2   = rcut*rcut;
  alfcut *= DEG2RAD;
  
  /* initialise topology */
  top = (t_topology *)malloc(sizeof(t_topology));
  init_topology(ftp2fn(efTPX,NFILE,fnm));

  /* initialise search array dah */
  init_dah(&dah,nr_dah,ftp2fn(efNDX,NFILE,fnm),ftp2fn(efTRX,NFILE,fnm));
  
  /* if mode is SELECTED or INSERT 
   * move all hbonds in dah list to hbond list 
   */
  if ((mode==SELECTED)||(mode==INSERT)) {
    list.nosearch(dah,nr_dah);
  }
  else {
    /* scan trajectory for all hydrogen bonds */
    fprintf(stderr,"Scanning for all hydrogen bonds\n");
    read_first_x(&status,ftp2fn(efTRX,NFILE,fnm),&this_time,&x,box);
    do {
      fprintf(stderr," # hbonds: %5d",list.search(dah,nr_dah));
    } while (read_next_x(status,&this_time,natoms,x,box));           
    for(i=0;(i<nr_dah);i++)
      delete (dah[i]);
    free(dah);
    
    rewind_trj(status);
  }

  /* analyse all the hydrogen bonds */
  list.analyse_init();

  /* do the final analysis */
  fprintf(stderr,"Analysing hydrogens bonds\n");
  read_first_x(&status,ftp2fn(efTRX,NFILE,fnm),&this_time,&x,box);
  for(i=0;(i<nr_frames) ;i++) {
    list.analyse();
    if (!read_next_x(status,&this_time,natoms,x,box))
      break;
  }         
  if (i != nr_frames)
    fprintf(stderr,
	    "WARNING: trajectory does not contain the same number of frames "
	    "as when it was read for the first time!\n");
    
  nr_frames=i;

  close_trj(status);
  
  /* print output of everything */
  list.dump(&(top->atoms));
    
  /* dump statistics of all hydrogen bonds */
  if (ftp2bSet(efOUT,NFILE,fnm)) {
    FILE *fp;
    fp = ffopen(ftp2fn(efOUT,NFILE,fnm),"w");
    list.print(fp);
    fclose(fp);
  }

  /* thank the audience */
  thanx(stdout);
  
  /* return to the base */
  return 0;
}
#endif	/* _g_hbond_cc */


