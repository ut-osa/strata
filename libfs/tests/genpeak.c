//==================================================== file = genpeak.c =====
//=  Program to generate peak distributed random variables                  =
//===========================================================================
//=  Notes: 1) Writes to a user specified output file                       =
//=         2) Generates user specified number of values                    =
//=         3) This distribution is developed i G. Pererra, K. Christensen, =
//=            and A. Roginsky, "Targeted Search: Reducing the Time and     =
//=            Cost for Searching for Objects in Multiple-Server Networks," =
//=            Proceedings of the International Performance Computing and   =
//=            Communications Conference, pp. 143-149, April 2005.          =
//=-------------------------------------------------------------------------=
//= Example user input:                                                     =
//=                                                                         =
//=  ---------------------------------------- genpeak.c -----               =
//=  -  Program to generate peak distributed RVs            -               =
//=  --------------------------------------------------------               =
//=  Output file name ===================================> x                =
//=  Random number seed =================================> 1                =
//=  N value ============================================> 10               =
//=  K value ============================================> 10               =
//=  Number of values to generate =======================> 10               =
//=  --------------------------------------------------------               =
//=  -  Generating samples to file                          -               =
//=  --------------------------------------------------------               =
//=  --------------------------------------------------------               =
//=  -  Done!                                                               =
//=  --------------------------------------------------------               =                                                        =
//=-------------------------------------------------------------------------=
//= Example output file ("output.dat" for above):                           =
//=                                                                         =
//=   1                                                                     =
//=   1                                                                     =
//=   6                                                                     =
//=   1                                                                     =
//=   2                                                                     =
//=   1                                                                     =
//=   1                                                                     =
//=   4                                                                     =
//=   4                                                                     =
//=   9                                                                     =
//=-------------------------------------------------------------------------=
//=  Build: bcc32 genpeak.c                                                 =
//=-------------------------------------------------------------------------=
//=  Execute: genpeak                                                       =
//=-------------------------------------------------------------------------=
//=  Author: Ken Christensen                                                =
//=          University of South Florida                                    =
//=          WWW: http://www.csee.usf.edu/~christen                         =
//=          Email: christen@csee.usf.edu                                   =
//=-------------------------------------------------------------------------=
//=  History: KJC (01/26/07) - Genesis (from genunifd.c)                    =
//===========================================================================

//----- Include files -------------------------------------------------------
#include <stdio.h>              // Needed for printf()
#include <stdlib.h>             // Needed for exit() and ato*()

//----- Function prototypes -------------------------------------------------
int    peak(int N, int K);      // Returns a peaked RV
double rand_valc(int seed);     // Jain's RNG to return 0 < z < 1

//===== Main program ========================================================
void main(void)
{
  FILE     *fp;                 // File pointer to output file
  char     file_name[256];      // Output file name string
  char     temp_string[256];    // Temporary string variable
  int      N;                   // N value
  int      K;                   // K value
  int      peak_rv;             // Peaked random variable
  int      num_values;          // Number of values
  int      i;                   // Loop counter

  // Output banner
  printf("---------------------------------------- genpeak.c ----- \n");
  printf("-  Program to generate peak distributed RVs            - \n");
  printf("-------------------------------------------------------- \n");

  // Prompt for output filename and then create/open the file
  printf("Output file name ===================================> ");
  scanf("%s", file_name);
  fp = fopen(file_name, "w");
  if (fp == NULL)
  {
    printf("ERROR in creating output file (%s) \n", file_name);
    exit(1);
  }

  // Prompt for random number seed and then use it
  printf("Random number seed =================================> ");
  scanf("%s", temp_string);
  rand_valc((int) atoi(temp_string));

  // Prompt for N value
  printf("N value ============================================> ");
  scanf("%s", temp_string);
  N = atoi(temp_string);

  // Prompt for K value
  printf("K value ============================================> ");
  scanf("%s", temp_string);
  K = atoi(temp_string);

  // Prompt for number of values to generate
  printf("Number of values to generate =======================> ");
  scanf("%s", temp_string);
  num_values = atoi(temp_string);

  //Output message and generate interarrival times
  printf("-------------------------------------------------------- \n");
  printf("-  Generating samples to file                          - \n");
  printf("-------------------------------------------------------- \n");

  // Generate and output interarrival times
  for (i=0; i<num_values; i++)
  {
    peak_rv = peak(N, K);
    fprintf(fp, "%d \n", peak_rv);
  }

  //Output message and close the output file
  printf("-------------------------------------------------------- \n");
  printf("-  Done! \n");
  printf("-------------------------------------------------------- \n");
  fclose(fp);
}

//===========================================================================
//=  Function to generate peak distributed random variables                 =
//=    - Input:  N and K values                                             =
//=    - Output: Returns with peak distributed random variable              =
//===========================================================================
int peak(int N, int K)
{
  double z;                     // Uniform random number (0 < z < 1)
  int    peak_value;            // Computed peaked value to be returned
  int    i;                     // Loop counter

  // Pull a uniform random value (0 < z < 1)
  z = rand_valc(0);

  // Generate peak RV -- similar approach to an empirical distribution
  if (z < ((double) K / (N + K - 1)))
    peak_value = 1;
  else
  {
    for (i=1; i<N; i++)
      if (z < ((double) (K + i) / (N + K - 1)))
      {
        peak_value = i + 1;
        break;
      }
  }

  return(peak_value);
}

//=========================================================================
//= Multiplicative LCG for generating uniform(0.0, 1.0) random numbers    =
//=   - From R. Jain, "The Art of Computer Systems Performance Analysis," =
//=     John Wiley & Sons, 1991. (Page 443, Figure 26.2)                  =
//=========================================================================
double rand_valc(int seed)
{
  const long  a =      16807;  // Multiplier
  const long  m = 2147483647;  // Modulus
  const long  q =     127773;  // m div a
  const long  r =       2836;  // m mod a
  static long x;               // Random int value
  long        x_div_q;         // x divided by q
  long        x_mod_q;         // x modulo q
  long        x_new;           // New x value

  // Set the seed if argument is non-zero and then return zero
  if (seed > 0)
  {
    x = seed;
    return(0.0);
  }

  // RNG using integer arithmetic
  x_div_q = x / q;
  x_mod_q = x % q;
  x_new = (a * x_mod_q) - (r * x_div_q);
  if (x_new > 0)
    x = x_new;
  else
    x = x_new + m;

  // Return a random value between 0.0 and 1.0
  return((double) x / m);
}
