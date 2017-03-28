// ==================================================================== //
//                                                                      //
//  demo.c -- qubit operarion demonstrator for QuEST                    //
//                                                                      //
// ==================================================================== //

# include <mpi.h>
# include <stdio.h>
# include <stdlib.h>
# include <time.h>
# include <math.h>
# include <unistd.h>
# include <string.h>
# include <omp.h>

# include "QUEST/qubits.h"
# include "QUEST/qubits_mpi.h"

# define MaxAngles      10
# define maxNumQubits   40
# define N_TRIALS 20
# define REPORT_TIMING 1
# define REPORT_STATE 1
# define INIT_COMMUNICATION 0

const long double Pi = 3.14159265358979323846264338327950288419716939937510;


// ==================================================================== //
//                                                                      //
//     system_timer -- precision walltime function, computes            //
//                     walltime based on the gettimeofday() function    //
//                                                                      //
// ==================================================================== //

double system_timer (void) {

# include <stdlib.h>
# include <sys/time.h>

	struct timeval time;

	gettimeofday (&time, NULL);

	return time.tv_sec + time.tv_usec / 1000000.0;

}

//--------------------------------------------------------------
//---------------------- START OF main()  ----------------------
//--------------------------------------------------------------
int main (int narg, char** varg) {

	int numQubits;
	long int index;

	Circuit circuit;

	double ang1,ang2,ang3;
	double aRe, aIm;
	double bRe, bIm;

	double stateProb,randProb;

	// init MPI environment
	int rank, numRanks;
	int provided;
	char hostName[256];
	int hostNameLen;
	MPI_Init_thread(&narg, &varg, MPI_THREAD_FUNNELED, &provided);
	MPI_Comm_size(MPI_COMM_WORLD, &numRanks);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	MPI_Get_processor_name(hostName, &hostNameLen); 
	printf("hostname on rank %d: %s\n", rank, hostName);

	// timing variables
	double wtime_start,
	       wtime_stop;
	double *timingVec;
	int trial;


	// define rotation angles
	double angles[MaxAngles][3] = {
		{ 1.2320,  0.4230, -0.6523},
		{ 2.1213,  0.0000,  3.6520},
		{-3.1213,  5.0230,  0.1230},
		{ 5.2341, -3.1001, -1.2340},
		{-0.1234, -0.9876,  4.1234}
	};
	int numAngles=5,iAngle;

	int rotQubit,measureQubit;
	long int numAmps, numAmpsPerRank;
	//int numRanks=1, rank=0; //! fix -- debug only

	// number of qubits is command line argument
	if (narg >= 2) {
		numQubits = atoi(varg[1]);
		if (numQubits < 1 || numQubits > maxNumQubits) {
			printf(" *** error: argument %d out of range (1 -- %d)\n", numQubits,maxNumQubits);
			exit (EXIT_FAILURE);
		}
	} else {
		printf(" *** error: too few arguments, number of qubits expected\n");
		exit (EXIT_FAILURE);
	}

	// divide up amps evenly between ranks
	numAmps = 1L << numQubits;
	numAmpsPerRank = numAmps/numRanks;

	if (rank==0){
	printf("Demo of single qubit rotations.\n");
	printf("Number of qubits is %d.\n", numQubits);
	printf("Number of amps is %ld.\n", numAmps);
	printf("Number of amps per rank is %ld.\n", numAmpsPerRank);
	}

	// allocate memory 
	if (REPORT_TIMING && rank==0) timingVec = malloc(N_TRIALS*numQubits*sizeof(timingVec));
	
	allocCircuit(&circuit, numQubits, rank, numRanks);
	printf("alloced mem\n");

	// initialise the state to |0000..0>
	initStateVec (&circuit);

	printf("initialized state\n");


	//
	// ===== apply rotations
	//
	/* // keep time */
	/* wtime_start = system_timer (); */

	// rotate
	ang1 = angles[0][0];
	ang2 = angles[0][1];
	ang3 = angles[0][2];

	aRe = cos(ang1) * cos(ang2);
	aIm = cos(ang1) * sin(ang2);
	bRe  = sin(ang1) * cos(ang3);
	bIm  = sin(ang1) * sin(ang3);

	// prepare files for writing output state vector and timing data
	FILE *timing, *distribution;
	char filename[100];

	if (REPORT_TIMING && rank==0){	
		sprintf(filename, "timing.csv");
		timing = fopen(filename, "w");
		fprintf(timing, "qubit, time(s), standardDevUp, standardDevLo, max, min\n");

		sprintf(filename, "distribution.csv");
		distribution = fopen(filename, "w");
	}


	// do a big MPI communication to get around first send/recv in the program occasionally taking many times longer
	//(due to MPI setup?)
	if (REPORT_TIMING && INIT_COMMUNICATION){
		rotateQubit(numQubits-1,aRe,aIm,bRe,bIm,&circuit);
		MPI_Barrier(MPI_COMM_WORLD);
	}

	for (rotQubit=0; rotQubit<numQubits; rotQubit++) {
	//for (rotQubit=3*numQubits/4; rotQubit<numQubits; rotQubit++) {
	//for (rotQubit=2; rotQubit<2; rotQubit++) {
		for (trial=0; trial<N_TRIALS; trial++){
			//! for timing -- have all ranks start at same place
			if (REPORT_TIMING) MPI_Barrier(MPI_COMM_WORLD);
			if (REPORT_TIMING && rank==0) wtime_start = system_timer ();

			// do rotation of each qubit N_TRIALS times for timing
			rotateQubit(rotQubit,aRe,aIm,bRe,bIm, &circuit);

			if (REPORT_TIMING) MPI_Barrier(MPI_COMM_WORLD);
                        if (REPORT_TIMING && rank==0) {
				wtime_stop = system_timer ();
				timingVec[trial*numQubits + rotQubit]=wtime_stop-wtime_start;
			}
		}
	}

printf("rotated\n");	
	// check vector size is unchanged
        double totalProbability;
	totalProbability = calcTotalProbability(circuit);
        if (rank==0) printf("total probability: %.14f\n", totalProbability);
printf("calc prob\n");

	// report timing to file
        if (REPORT_TIMING && rank==0){
                double totTime, avg, standardDev, temp, max, min;
                for(index=0; index<numQubits; index++){
                        max=0; min=10e5;
                        totTime=0;
                        for (trial=0; trial<N_TRIALS; trial++){
                                temp=timingVec[trial*numQubits + index];
                                totTime+=temp;
                                if (temp<min) min=temp;
                                if (temp>max) max=temp;
                                if (index==numQubits-2) fprintf(distribution, "%.8f\n", timingVec[trial*numQubits + index]);
                        }
                        avg = totTime/(double)N_TRIALS;
                        standardDev=0;
                        for (trial=0; trial<N_TRIALS; trial++){
                                temp = timingVec[trial*numQubits + index]-avg;
                                standardDev += temp*temp;
                        }
                        standardDev = sqrt(standardDev/(double)N_TRIALS);
                        fprintf(timing, "%d, %.8f, %.8f, %.8f, %.8f, %.8f\n", index, avg, avg+3*standardDev, avg-3*standardDev, max, min);
                }
        }

        // report state vector to file
	if (REPORT_STATE){
		reportState(circuit);
        }


	//
	// ===== perform a measurement
	//
	/* // keep time */
	/* wtime_start = system_timer (); */

	// measure
/*	
	for (measureQubit=0; measureQubit<numQubits; measureQubit++) {
	//for (measureQubit=0; measureQubit<1; measureQubit++) {
		MPI_Barrier(MPI_COMM_WORLD);
		wtime_start = system_timer ();
		stateProb = findProbabilityOfZero (rank, numAmpsPerRank, numQubits, measureQubit, stateVecReal,stateVecImag);
		MPI_Barrier(MPI_COMM_WORLD);
		wtime_stop = system_timer ();
		if (rank==0) printf("   probability of 0 for qubit %d = %.14f\n", measureQubit, stateProb);
		if (rank==0) printf(" measurement qubit %d: elapsed time = %f [s]\n", measureQubit, wtime_stop - wtime_start);
	}
*/
	/* // keep time */
	/* wtime_stop = system_timer (); */

	/* // ----- timing report */
	/* printf(" measurement: elapsed time = %f [s]\n", wtime_stop - wtime_start); */


	//
	// ===== two qubit phase gate
	//
	/* // keep time */
	/* wtime_start = system_timer (); */
/*
	// two qubit phase gate
	if (numQubits >= 7) {
		wtime_start = system_timer (); 
		controlPhaseGate (rank, numAmpsPerRank, numQubits, 0, 2, stateVecReal, stateVecImag); 
		wtime_stop = system_timer (); 
		printf(" two qubit phase gate: elapsed time = %f [s]\n", wtime_stop - wtime_start);
		wtime_start = system_timer (); controlPhaseGate (rank, numAmpsPerRank, numQubits, 1, 3, stateVecReal, stateVecImag); wtime_stop = system_timer (); printf(" two qubit phase gate: elapsed time = %f [s]\n", wtime_stop - wtime_start);
		wtime_start = system_timer (); controlPhaseGate (rank, numAmpsPerRank, numQubits, 2, 4, stateVecReal, stateVecImag); wtime_stop = system_timer (); printf(" two qubit phase gate: elapsed time = %f [s]\n", wtime_stop - wtime_start);
		wtime_start = system_timer (); controlPhaseGate (rank, numAmpsPerRank, numQubits, 3, 5, stateVecReal, stateVecImag); wtime_stop = system_timer (); printf(" two qubit phase gate: elapsed time = %f [s]\n", wtime_stop - wtime_start);
		wtime_start = system_timer (); controlPhaseGate (rank, numAmpsPerRank, numQubits, 4, 6, stateVecReal, stateVecImag); wtime_stop = system_timer (); printf(" two qubit phase gate: elapsed time = %f [s]\n", wtime_stop - wtime_start);
	}
*/
	totalProbability = calcTotalProbability(circuit);
        if (rank==0) printf("total probability: %.14f\n", totalProbability);
/*
if (rank==0){
	printf("\n\nIn rank %d, the following is the final state after rotations.\n\n",rank);
	printf("codeOutput=[\n");
	for(index=0; index<=numAmpsPerRank-1; index++) printf("%.8f %.8f\n",stateVecReal[index],stateVecImag[index]);
	printf("];\n\n");
}
MPI_Barrier(MPI_COMM_WORLD);

if (rank==1){
	printf("\n\nIn rank %d, the following is the final state after rotations.\n\n",rank);
	printf("codeOutput=[\n");
	for(index=0; index<=numAmpsPerRank-1; index++) printf("%.8f %.8f\n",stateVecReal[index],stateVecImag[index]);
	printf("];\n\n");
}
MPI_Barrier(MPI_COMM_WORLD);
*/
	/* // keep time */
	/* wtime_stop = system_timer (); */

	/* // ----- timing report */
	/* printf(" two qubit phase gate: elapsed time = %f [s]\n", wtime_stop - wtime_start); */


	// free memory
	//fclose(state);
	if (REPORT_TIMING && rank==0) fclose(timing);
	if (REPORT_TIMING && rank==0) fclose(distribution);

	freeCircuit(&circuit);

	if (REPORT_TIMING && rank==0) free(timingVec);

	MPI_Finalize();

	return EXIT_SUCCESS;
}

//--------------------------------------------------------------
//----------------------- END OF main()  -----------------------
//--------------------------------------------------------------