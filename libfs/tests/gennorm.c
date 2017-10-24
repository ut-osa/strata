//==================================================== file = gennorm.c =====
//=  Program to generate nomrally distributed random variables              =
//===========================================================================
//=  Notes: 1) Writes to a user specified output file                       =
//=         2) Generates user specified number of samples                   =
//=         3) Uses the Box-Muller method and only generates one of two     =
//=            paired normal random variables.                              =
//=-------------------------------------------------------------------------=
//= Example user input:                                                     =
//=                                                                         =
//=   ---------------------------------------- gennorm.c -----              =
//=   -  Program to generate normally distributed random     -              =
//=   -  variables                                           -              =
//=   --------------------------------------------------------              =
//=   Output file name ===================================> output.dat      =
//=   Random number seed =================================> 1               =
//=   Mean ===============================================> 0               =
//=   Standard deviation =================================> 1               =
//=   Number of samples to generate ======================> 10              =
//=   --------------------------------------------------------              =
//=   -  Generating samples to file                          -              =
//=   --------------------------------------------------------              =
//=   --------------------------------------------------------              =
//=   -  Done!                                                              =
//=   --------------------------------------------------------              =
//=-------------------------------------------------------------------------=
//= Example output (from above user input):                                 =
//=                                                                         =
//=  3.015928                                                               =
//=  1.446444                                                               =
//=  0.294214                                                               =
//=  0.372630                                                               =
//=  0.802585                                                               =
//=  -1.509856                                                              =
//=  -0.672829                                                              =
//=  1.033490                                                               =
//=  0.759008                                                               =
//=  0.078499                                                               =
//=-------------------------------------------------------------------------=
//=  Build: bcc32 gennorm.c                                                 =
//=-------------------------------------------------------------------------=
//=  Execute: gennorm                                                       =
//=-------------------------------------------------------------------------=
//=  Author: Kenneth J. Christensen                                         =
//=          University of South Florida                                    =
//=          WWW: http://www.csee.usf.edu/~christen                         =
//=          Email: christen@csee.usf.edu                                   =
//=-------------------------------------------------------------------------=
//=  History: KJC (06/06/02) - Genesis                                      =
//=           KJC (05/20/03) - Added Jain's RNG for finer granularity       =
//===========================================================================

//----- Include files -------------------------------------------------------
#include <stdio.h>              // Needed for printf()
#include <stdlib.h>             // Needed for exit() and ato*()
#include <math.h>               // Needed for sqrt() and log()

//----- Defines -------------------------------------------------------------
#define PI         3.14159265   // The value of pi

//----- Function prototypes -------------------------------------------------
double norm(double mean, double std_dev);  // Returns a normal rv
double rand_val(int seed);                 // Jain's RNG

//===== Main program ========================================================
void main(void)
{
  FILE     *fp_out;               // File pointer to output file
  char     instring[80];          // Input string
  double   num_samples;           // Number of samples to generate
  double   mean, std_dev;         // Mean and standard deviation
  double   norm_rv;               // The adjusted normal rv
  int      i;                     // Loop counter

  // Output banner
  printf("---------------------------------------- gennorm.c ----- \n");
  printf("-  Program to generate normally distributed random     - \n");
  printf("-  variables                                           - \n");
  printf("-------------------------------------------------------- \n");

  // Prompt for output filename and then create/open the file
  printf("Output file name ===================================> ");
  scanf("%s", instring);
  fp_out = fopen(instring, "w");
  if (fp_out == NULL)
  {
    printf("ERROR in creating output file (%s) \n", instring);
    exit(1);
  }

  // Prompt for random number seed and then use it
  printf("Random number seed =================================> ");
  scanf("%s", instring);
  rand_val((int) atoi(instring));

  // Prompt for mean value
  printf("Mean ===============================================> ");
  scanf("%s", instring);
  mean = atof(instring);

  // Prompt for standard deviation
  printf("Standard deviation =================================> ");
  scanf("%s", instring);
  std_dev = atof(instring);

  // Prompt for number of samples to generate
  printf("Number of samples to generate ======================> ");
  scanf("%s", instring);
  num_samples = atoi(instring);

  // Output message and generate interarrival times
  printf("-------------------------------------------------------- \n");
  printf("-  Generating samples to file                          - \n");
  printf("-------------------------------------------------------- \n");
  for (i=0; i<num_samples; i++)
  {
    // Generate a normally distributed rv
    norm_rv = norm(mean, std_dev);

    // Output the norm_rv value
    fprintf(fp_out, "%f \n", norm_rv);
  }

  // Output message and close the distribution and output files
  printf("-------------------------------------------------------- \n");
  printf("-  Done! \n");
  printf("-------------------------------------------------------- \n");
  fclose(fp_out);
}

//===========================================================================
//=  Function to generate normally distributed random variable using the    =
//=  Box-Muller method                                                      =
//=    - Input: mean and standard deviation                                 =
//=    - Output: Returns with normally distributed random variable          =
//===========================================================================
double norm(double mean, double std_dev)
{
  double   u, r, theta;           // Variables for Box-Muller method
  double   x;                     // Normal(0, 1) rv
  double   norm_rv;               // The adjusted normal rv

  // Generate u
  u = 0.0;
  while (u == 0.0)
    u = rand_val(0);

  // Compute r
  r = sqrt(-2.0 * log(u));

  // Generate theta
  theta = 0.0;
  while (theta == 0.0)
    theta = 2.0 * PI * rand_val(0);

  // Generate x value
  x = r * cos(theta);

  // Adjust x value for specified mean and variance
  norm_rv = (x * std_dev) + mean;

  // Return the normally distributed RV value
  return(norm_rv);
}

//=========================================================================
//= Multiplicative LCG for generating uniform(0.0, 1.0) random numbers    =
//=   - x_n = 7^5*x_(n-1)mod(2^31 - 1)                                    =
//=   - With x seeded to 1 the 10000th x value should be 1043618065       =
//=   - From R. Jain, "The Art of Computer Systems Performance Analysis," =
//=     John Wiley & Sons, 1991. (Page 443, Figure 26.2)                  =
//=========================================================================
double rand_val(int seed)
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
