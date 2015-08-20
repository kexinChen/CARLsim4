/* * Copyright (c) 2015 Regents of the University of California. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. The names of its contributors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * *********************************************************************************************** *
 * CARLsim
 * created by: (MDR) Micah Richert, (JN) Jayram M. Nageswaran
 * maintained by:
 * (MA) Mike Avery <averym@uci.edu>
 * (MB) Michael Beyeler <mbeyeler@uci.edu>,
 * (KDC) Kristofor Carlson <kdcarlso@uci.edu>
 * (TSC) Ting-Shuo Chou <tingshuc@uci.edu>
 *
 * CARLsim available from http://socsci.uci.edu/~jkrichma/CARLsim/
 * Ver 5/22/2015
 */

#include <snn.h>
#include <sstream>
#include <algorithm>

#include <connection_monitor.h>
#include <connection_monitor_core.h>
#include <spike_monitor.h>
#include <spike_monitor_core.h>
#include <group_monitor.h>
#include <group_monitor_core.h>

// \FIXME what are the following for? why were they all the way at the bottom of this file?

#define COMPACTION_ALIGNMENT_PRE  16
#define COMPACTION_ALIGNMENT_POST 0

/// **************************************************************************************************************** ///
/// CONSTRUCTOR / DESTRUCTOR
/// **************************************************************************************************************** ///


// TODO: consider moving unsafe computations out of constructor
SNN::SNN(const std::string& name, SimMode simMode, LoggerMode loggerMode, int numGPUs, int randSeed)
					: networkName_(name), simMode_(simMode), loggerMode_(loggerMode), numGPUs_(numGPUs),
					  randSeed_(SNN::setRandSeed(randSeed)) // all of these are const
{
	// move all unsafe operations out of constructor
	SNNinit();
}

// destructor
SNN::~SNN() {
	if (!simulatorDeleted)
		deleteObjects();
}



/// ************************************************************************************************************ ///
/// PUBLIC METHODS: SETTING UP A SIMULATION
/// ************************************************************************************************************ ///

// make from each neuron in grpId1 to 'numPostSynapses' neurons in grpId2
short int SNN::connect(int grpId1, int grpId2, const std::string& _type, float initWt, float maxWt, float prob,
						uint8_t minDelay, uint8_t maxDelay, float radX, float radY, float radZ,
						float _mulSynFast, float _mulSynSlow, bool synWtType) {
						//const std::string& wtType
	int retId=-1;
	assert(grpId1 < numGroups);
	assert(grpId2 < numGroups);
	assert(minDelay <= maxDelay);
	assert(!isPoissonGroup(grpId2));

    //* \deprecated Do these ramp thingies still work?
//    bool useRandWts = (wtType.find("random") != std::string::npos);
//    bool useRampDownWts = (wtType.find("ramp-down") != std::string::npos);
//    bool useRampUpWts = (wtType.find("ramp-up") != std::string::npos);
//    uint32_t connProp = SET_INITWTS_RANDOM(useRandWts)
//      | SET_CONN_PRESENT(1)
//      | SET_FIXED_PLASTIC(synWtType)
//      | SET_INITWTS_RAMPUP(useRampUpWts)
//      | SET_INITWTS_RAMPDOWN(useRampDownWts);
	uint32_t connProp = SET_CONN_PRESENT(1) | SET_FIXED_PLASTIC(synWtType);

	Grid3D szPre = getGroupGrid3D(grpId1);
	Grid3D szPost = getGroupGrid3D(grpId2);

	// initialize configuration of a connection
	ConnectConfig connConfig;

	connConfig.grpSrc   		  = grpId1;
	connConfig.grpDest  		  = grpId2;
	connConfig.initWt	  		  = initWt;
	connConfig.maxWt	  		  = maxWt;
	connConfig.maxDelay 		  = maxDelay;
	connConfig.minDelay 		  = minDelay;
//		newInfo->radX             = (radX<0) ? MAX(szPre.x,szPost.x) : radX; // <0 means full connectivity, so the
//		newInfo->radY             = (radY<0) ? MAX(szPre.y,szPost.y) : radY; // effective group size is Grid3D.x. Grab
//		newInfo->radZ             = (radZ<0) ? MAX(szPre.z,szPost.z) : radZ; // the larger of pre / post to connect all
	connConfig.radX             = radX;
	connConfig.radY             = radY;
	connConfig.radZ             = radZ;
	connConfig.mulSynFast       = _mulSynFast;
	connConfig.mulSynSlow       = _mulSynSlow;
	connConfig.connProp         = connProp;
	connConfig.p                = prob;
	connConfig.type             = CONN_UNKNOWN;
	connConfig.connectionMonitorId = -1;
	connConfig.connId = -1;
	connConfig.conn = NULL;
	connConfig.newUpdates = false;
	connConfig.numberOfConnections = 0;

	if ( _type.find("random") != std::string::npos) {
		connConfig.type = CONN_RANDOM;
	}
	//so you're setting the size to be prob*Number of synapses in group info + some standard deviation ...
	else if ( _type.find("full-no-direct") != std::string::npos) {
		connConfig.type	= CONN_FULL_NO_DIRECT;
	}
	else if ( _type.find("full") != std::string::npos) {
		connConfig.type = CONN_FULL;
	}
	else if ( _type.find("one-to-one") != std::string::npos) {
		connConfig.type = CONN_ONE_TO_ONE;
	} else if ( _type.find("gaussian") != std::string::npos) {
		connConfig.type   = CONN_GAUSSIAN;
	} else {
		KERNEL_ERROR("Invalid connection type (should be 'random', 'full', 'one-to-one', 'full-no-direct', or 'gaussian')");
		exitSimulation(-1);
	}

	// assign connection id
	assert(connConfig.connId == -1);
	connConfig.connId = numConnections;

	KERNEL_DEBUG("CONNECT SETUP: connId=%d, mulFast=%f, mulSlow=%f", connConfig.connId, connConfig.mulSynFast, connConfig.mulSynSlow);

	// store the configuration of a connection
	connectConfigMap[numConnections] = connConfig; // connConfig.connId == numConnections
	
	assert(numConnections < MAX_CONN_PER_SNN);	// make sure we don't overflow connId
	numConnections++;
	
	return (numConnections - 1);
}

// make custom connections from grpId1 to grpId2
short int SNN::connect(int grpId1, int grpId2, ConnectionGeneratorCore* conn, float _mulSynFast, float _mulSynSlow,
						bool synWtType) {
	int retId=-1;

	assert(grpId1 < numGroups);
	assert(grpId2 < numGroups);

	// initialize the configuration of a connection
	ConnectConfig connConfig;

	connConfig.grpSrc   = grpId1;
	connConfig.grpDest  = grpId2;
	connConfig.initWt	  = 1;
	connConfig.maxWt	  = 1;
	connConfig.maxDelay = MAX_SYN_DELAY;
	connConfig.minDelay = 1;
	connConfig.mulSynFast = _mulSynFast;
	connConfig.mulSynSlow = _mulSynSlow;
	connConfig.connProp = SET_CONN_PRESENT(1) | SET_FIXED_PLASTIC(synWtType);
	connConfig.type = CONN_USER_DEFINED;
	connConfig.conn = conn;
	connConfig.connectionMonitorId = -1;
	connConfig.connId = -1;
	connConfig.newUpdates = false;
	connConfig.numberOfConnections = 0;

	// assign a connection id
	assert(connConfig.connId == -1);
	connConfig.connId = numConnections;

	// store the configuration of a connection
	connectConfigMap[numConnections] = connConfig; // connConfig.connId == numConnections

	assert(numConnections < MAX_CONN_PER_SNN);	// make sure we don't overflow connId
	numConnections++;

	return (numConnections - 1);
}


// create group of Izhikevich neurons
// use int for nNeur to avoid arithmetic underflow
int SNN::createGroup(const std::string& grpName, const Grid3D& grid, int neurType) {
	assert(grid.numX * grid.numY * grid.numZ > 0);
	assert(neurType >= 0);
	assert(numGroups < MAX_GRP_PER_SNN);

	if ( (!(neurType & TARGET_AMPA) && !(neurType & TARGET_NMDA) &&
		  !(neurType & TARGET_GABAa) && !(neurType & TARGET_GABAb)) || (neurType & POISSON_NEURON)) {
		KERNEL_ERROR("Invalid type using createGroup... Cannot create poisson generators here.");
		exitSimulation(1);
	}

	//// initialize group configuration
	//GroupConfig grpConfig;
	//
	//// init parameters of neural group size and location
	//grpConfig.Name = grpName;
	//grpConfig.type = neurType;
	//grpConfig.numN = grid.N;;
	//grpConfig.sizeX = grid.numX;
	//grpConfig.sizeY = grid.numY;
	//grpConfig.sizeZ = grid.numZ;
	//grpConfig.distX = grid.distX;
	//grpConfig.distY = grid.distY;
	//grpConfig.distZ = grid.distZ;
	//grpConfig.offsetX = grid.offsetX;
	//grpConfig.offsetY = grid.offsetY;
	//grpConfig.offsetZ = grid.offsetZ;

	//// init parameters of neural group dynamics
	//grpConfig.Izh_a = -1.0f;
	//grpConfig.Izh_a_sd = -1.0f;
	//grpConfig.Izh_b = -1.0f;
	//grpConfig.Izh_b_sd = -1.0f;
	//grpConfig.Izh_c = -1.0f;
	//grpConfig.Izh_c_sd = -1.0f;
	//grpConfig.Izh_d = -1.0f;
	//grpConfig.Izh_d_sd = -1.0f;

	//grpConfig.isSpikeGenerator = false;

	//// init homeostatic plasticity configs
	//grpConfig.baseFiring = -1.0f;
	//grpConfig.baseFiringSD = -1.0f;
	//grpConfig.avgTimeScale = -1.0f;
	//grpConfig.avgTimeScaleDecay = -1.0f;
	//grpConfig.homeostasisScale = -1.0f;

	//// init parameters of neuromodulator
	//grpConfig.baseDP = -1.0f;
	//grpConfig.base5HT = -1.0f;
	//grpConfig.baseACh = -1.0f;
	//grpConfig.baseNE = -1.0f;
	//grpConfig.decayDP = -1.0f;
	//grpConfig.decay5HT = -1.0f;
	//grpConfig.decayACh = -1.0f;
	//grpConfig.decayNE = -1.0f;

	//groupConfigMap[numGroups] = grpConfig;

	GroupConfigRT grpConfig;
	initGroupConfig(&grpConfig);

	// We don't store the Grid3D struct in groupConfig so we don't have to deal with allocating structs on the GPU
	grpConfig.SizeN = grid.N; // number of neurons in the group
	grpConfig.SizeX = grid.numX; // number of neurons in first dim of Grid3D
	grpConfig.SizeY = grid.numY; // number of neurons in second dim of Grid3D
	grpConfig.SizeZ = grid.numZ; // number of neurons in third dim of Grid3D

	grpConfig.Type   			= neurType;
	grpConfig.WithSTP			= false;
	grpConfig.WithSTDP			= false;
	grpConfig.WithESTDPtype      = UNKNOWN_STDP;
	grpConfig.WithISTDPtype		= UNKNOWN_STDP;
	grpConfig.WithHomeostasis	= false;

	if ( (neurType&TARGET_GABAa) || (neurType&TARGET_GABAb)) {
		grpConfig.MaxFiringRate 	= INHIBITORY_NEURON_MAX_FIRING_RATE;
	} else {
		grpConfig.MaxFiringRate 	= EXCITATORY_NEURON_MAX_FIRING_RATE;
	}

	grpConfig.isSpikeGenerator	= false;
	grpConfig.MaxDelay			= 1;
	
	groupInfo[numGroups].Name  			= grpName;
	groupInfo[numGroups].Izh_a 			= -1; // \FIXME ???

	// init homeostasis params even though not used
	groupInfo[numGroups].baseFiring        = 10.0f;
	groupInfo[numGroups].baseFiringSD      = 0.0f;

	groupInfo[numGroups].Name              = grpName;

	// update number of neuron counters
	if ( (neurType&TARGET_GABAa) || (neurType&TARGET_GABAb))
		numNInhReg += grid.N; // regular inhibitory neuron
	else
		numNExcReg += grid.N; // regular excitatory neuron
	numNReg += grid.N;
	numN += grid.N;

	// assign a group id
	assert(grpConfig.grpId == -1);
	grpConfig.grpId = numGroups;

	// store the configuration of a group
	groupConfigMap[numGroups] = grpConfig; // numGroups == grpId

	assert(numGroups < MAX_GRP_PER_SNN); // make sure we don't overflow connId
	numGroups++;

	return (numGroups - 1);
}

// create spike generator group
// use int for nNeur to avoid arithmetic underflow
int SNN::createSpikeGeneratorGroup(const std::string& grpName, const Grid3D& grid, int neurType) {
	assert(grid.numX * grid.numY * grid.numZ > 0);
	assert(neurType >= 0);
	assert(numGroups < MAX_GRP_PER_SNN);

	//// initialize group configuration
	//GroupConfig grpConfig;
	//
	//// init parameters of neural group size and location
	//grpConfig.Name = grpName;
	//grpConfig.type = neurType | POISSON_NEURON;
	//grpConfig.numN = grid.N;;
	//grpConfig.sizeX = grid.numX;
	//grpConfig.sizeY = grid.numY;
	//grpConfig.sizeZ = grid.numZ;
	//grpConfig.distX = grid.distX;
	//grpConfig.distY = grid.distY;
	//grpConfig.distZ = grid.distZ;
	//grpConfig.offsetX = grid.offsetX;
	//grpConfig.offsetY = grid.offsetY;
	//grpConfig.offsetZ = grid.offsetZ;

	//// init parameters of neural group dynamics
	//grpConfig.Izh_a = -1.0f;
	//grpConfig.Izh_a_sd = -1.0f;
	//grpConfig.Izh_b = -1.0f;
	//grpConfig.Izh_b_sd = -1.0f;
	//grpConfig.Izh_c = -1.0f;
	//grpConfig.Izh_c_sd = -1.0f;
	//grpConfig.Izh_d = -1.0f;
	//grpConfig.Izh_d_sd = -1.0f;

	//grpConfig.isSpikeGenerator = true;

	//// init homeostatic plasticity configs
	//grpConfig.baseFiring = -1.0f;
	//grpConfig.baseFiringSD = -1.0f;
	//grpConfig.avgTimeScale = -1.0f;
	//grpConfig.avgTimeScaleDecay = -1.0f;
	//grpConfig.homeostasisScale = -1.0f;

	//// init parameters of neuromodulator
	//grpConfig.baseDP = -1.0f;
	//grpConfig.base5HT = -1.0f;
	//grpConfig.baseACh = -1.0f;
	//grpConfig.baseNE = -1.0f;
	//grpConfig.decayDP = -1.0f;
	//grpConfig.decay5HT = -1.0f;
	//grpConfig.decayACh = -1.0f;
	//grpConfig.decayNE = -1.0f;

	//groupConfigMap[numGroups] = grpConfig;

	GroupConfigRT grpConfig;
	initGroupConfig(&grpConfig);

	grpConfig.SizeN = grid.N; // number of neurons in the group
	grpConfig.SizeX = grid.numX; // number of neurons in first dim of Grid3D
	grpConfig.SizeY = grid.numY; // number of neurons in second dim of Grid3D
	grpConfig.SizeZ = grid.numZ; // number of neurons in third dim of Grid3D
	grpConfig.Type    		= neurType | POISSON_NEURON;
	grpConfig.WithSTP		= false;
	grpConfig.WithSTDP		= false;
	grpConfig.WithESTDPtype  = UNKNOWN_STDP;
	grpConfig.WithISTDPtype	= UNKNOWN_STDP;
	grpConfig.WithHomeostasis	= false;
	grpConfig.isSpikeGenerator	= true;		// these belong to the spike generator class...
	grpConfig.MaxFiringRate 	= POISSON_MAX_FIRING_RATE;

	groupInfo[numGroups].Name          = grpName;

	if ( (neurType&TARGET_GABAa) || (neurType&TARGET_GABAb))
		numNInhPois += grid.N; // inh poisson group
	else
		numNExcPois += grid.N; // exc poisson group
	numNPois += grid.N;
	numN += grid.N;

	// assign a group id
	assert(grpConfig.grpId == -1);
	grpConfig.grpId = numGroups;

	// store the configuration of a group
	groupConfigMap[numGroups] = grpConfig;

	assert(numGroups < MAX_GRP_PER_SNN); // make sure we don't overflow connId
	numGroups++;
	numSpikeGenGrps++;

	return (numGroups - 1);
}

// set conductance values for a simulation (custom values or disable conductances alltogether)
void SNN::setConductances(bool isSet, int tdAMPA, int trNMDA, int tdNMDA, int tdGABAa, int trGABAb, int tdGABAb) {
	if (isSet) {
		assert(tdAMPA>0); assert(tdNMDA>0); assert(tdGABAa>0); assert(tdGABAb>0);
		assert(trNMDA>=0); assert(trGABAb>=0); // 0 to disable rise times
		assert(trNMDA!=tdNMDA); assert(trGABAb!=tdGABAb); // singularity
	}

	// set conductances globally for all connections
	sim_with_conductances  |= isSet;
	dAMPA  = 1.0-1.0/tdAMPA;
	dNMDA  = 1.0-1.0/tdNMDA;
	dGABAa = 1.0-1.0/tdGABAa;
	dGABAb = 1.0-1.0/tdGABAb;

	if (trNMDA>0) {
		// use rise time for NMDA
		sim_with_NMDA_rise = true;
		rNMDA = 1.0-1.0/trNMDA;

		// compute max conductance under this model to scale it back to 1
		// otherwise the peak conductance will not be equal to the weight
		double tmax = (-tdNMDA*trNMDA*log(1.0*trNMDA/tdNMDA))/(tdNMDA-trNMDA); // t at which cond will be max
		sNMDA = 1.0/(exp(-tmax/tdNMDA)-exp(-tmax/trNMDA)); // scaling factor, 1 over max amplitude
		assert(!isinf(tmax) && !isnan(tmax) && tmax>=0);
		assert(!isinf(sNMDA) && !isnan(sNMDA) && sNMDA>0);
	}

	if (trGABAb>0) {
		// use rise time for GABAb
		sim_with_GABAb_rise = true;
		rGABAb = 1.0-1.0/trGABAb;

		// compute max conductance under this model to scale it back to 1
		// otherwise the peak conductance will not be equal to the weight
		double tmax = (-tdGABAb*trGABAb*log(1.0*trGABAb/tdGABAb))/(tdGABAb-trGABAb); // t at which cond will be max
		sGABAb = 1.0/(exp(-tmax/tdGABAb)-exp(-tmax/trGABAb)); // scaling factor, 1 over max amplitude
		assert(!isinf(tmax) && !isnan(tmax)); assert(!isinf(sGABAb) && !isnan(sGABAb) && sGABAb>0);
	}

	if (sim_with_conductances) {
		KERNEL_INFO("Running COBA mode:");
		KERNEL_INFO("  - AMPA decay time            = %5d ms", tdAMPA);
		KERNEL_INFO("  - NMDA rise time %s  = %5d ms", sim_with_NMDA_rise?"          ":"(disabled)", trNMDA);
		KERNEL_INFO("  - GABAa decay time           = %5d ms", tdGABAa);
		KERNEL_INFO("  - GABAb rise time %s = %5d ms", sim_with_GABAb_rise?"          ":"(disabled)",trGABAb);
		KERNEL_INFO("  - GABAb decay time           = %5d ms", tdGABAb);
	} else {
		KERNEL_INFO("Running CUBA mode (all synaptic conductances disabled)");
	}
}

// set homeostasis for group
void SNN::setHomeostasis(int grpId, bool isSet, float homeoScale, float avgTimeScale) {
	if (grpId == ALL) { // shortcut for all groups
		for(int grpId1=0; grpId1<numGroups; grpId1++) {
			setHomeostasis(grpId1, isSet, homeoScale, avgTimeScale);
		}
	} else {
		// set conductances for a given group
		sim_with_homeostasis 			   |= isSet;
		groupConfigMap[grpId].WithHomeostasis    = isSet;
		groupConfigMap[grpId].homeostasisScale   = homeoScale;
		groupConfigMap[grpId].avgTimeScale       = avgTimeScale;
		groupConfigMap[grpId].avgTimeScaleInv    = 1.0f/avgTimeScale;
		groupConfigMap[grpId].avgTimeScale_decay = (avgTimeScale*1000.0f-1.0f)/(avgTimeScale*1000.0f);
		groupConfigMap[grpId].newUpdates 		= true; // \FIXME: what's this?

		KERNEL_INFO("Homeostasis parameters %s for %d (%s):\thomeoScale: %f, avgTimeScale: %f",
					isSet?"enabled":"disabled",grpId,groupInfo[grpId].Name.c_str(),homeoScale,avgTimeScale);
	}
}

// set a homeostatic target firing rate (enforced through homeostatic synaptic scaling)
void SNN::setHomeoBaseFiringRate(int grpId, float baseFiring, float baseFiringSD) {
	if (grpId == ALL) { // shortcut for all groups
		for(int grpId1=0; grpId1<numGroups; grpId1++) {
			setHomeoBaseFiringRate(grpId1, baseFiring, baseFiringSD);
		}
	} else {
		// set conductances for a given group
		assert(groupConfigMap[grpId].WithHomeostasis);

		groupInfo[grpId].baseFiring 	= baseFiring;
		groupInfo[grpId].baseFiringSD 	= baseFiringSD;
		groupConfigMap[grpId].newUpdates 	= true; //TODO: I have to see how this is handled.  -- KDC

		KERNEL_INFO("Homeostatic base firing rate set for %d (%s):\tbaseFiring: %3.3f, baseFiringStd: %3.3f",
							grpId,groupInfo[grpId].Name.c_str(),baseFiring,baseFiringSD);
	}
}


// set Izhikevich parameters for group
void SNN::setNeuronParameters(int grpId, float izh_a, float izh_a_sd, float izh_b, float izh_b_sd,
								float izh_c, float izh_c_sd, float izh_d, float izh_d_sd)
{
	assert(grpId>=-1); assert(izh_a_sd>=0); assert(izh_b_sd>=0); assert(izh_c_sd>=0);
	assert(izh_d_sd>=0);

	if (grpId == ALL) { // shortcut for all groups
		for(int grpId1=0; grpId1<numGroups; grpId1++) {
			setNeuronParameters(grpId1, izh_a, izh_a_sd, izh_b, izh_b_sd, izh_c, izh_c_sd, izh_d, izh_d_sd);
		}
	} else {
		groupInfo[grpId].Izh_a	  	=   izh_a;
		groupInfo[grpId].Izh_a_sd  =   izh_a_sd;
		groupInfo[grpId].Izh_b	  	=   izh_b;
		groupInfo[grpId].Izh_b_sd  =   izh_b_sd;
		groupInfo[grpId].Izh_c		=   izh_c;
		groupInfo[grpId].Izh_c_sd	=   izh_c_sd;
		groupInfo[grpId].Izh_d		=   izh_d;
		groupInfo[grpId].Izh_d_sd	=   izh_d_sd;
	}
}

void SNN::setNeuromodulator(int grpId, float baseDP, float tauDP, float base5HT, float tau5HT, float baseACh,
	float tauACh, float baseNE, float tauNE) {

	groupConfigMap[grpId].baseDP	= baseDP;
	groupConfigMap[grpId].decayDP = 1.0 - (1.0 / tauDP);
	groupConfigMap[grpId].base5HT = base5HT;
	groupConfigMap[grpId].decay5HT = 1.0 - (1.0 / tau5HT);
	groupConfigMap[grpId].baseACh = baseACh;
	groupConfigMap[grpId].decayACh = 1.0 - (1.0 / tauACh);
	groupConfigMap[grpId].baseNE	= baseNE;
	groupConfigMap[grpId].decayNE = 1.0 - (1.0 / tauNE);
}

// set ESTDP params
void SNN::setESTDP(int grpId, bool isSet, STDPType type, STDPCurve curve, float alphaPlus, float tauPlus, float alphaMinus, float tauMinus, float gamma) {
	assert(grpId>=-1);
	if (isSet) {
		assert(type!=UNKNOWN_STDP);
		assert(tauPlus>0.0f); assert(tauMinus>0.0f); assert(gamma>=0.0f);
	}

	if (grpId == ALL) { // shortcut for all groups
		for(int grpId1=0; grpId1<numGroups; grpId1++) {
			setESTDP(grpId1, isSet, type, curve, alphaPlus, tauPlus, alphaMinus, tauMinus, gamma);
		}
	} else {
		// set STDP for a given group
		// set params for STDP curve
		groupConfigMap[grpId].ALPHA_PLUS_EXC 		= alphaPlus;
		groupConfigMap[grpId].ALPHA_MINUS_EXC 	= alphaMinus;
		groupConfigMap[grpId].TAU_PLUS_INV_EXC 	= 1.0f/tauPlus;
		groupConfigMap[grpId].TAU_MINUS_INV_EXC	= 1.0f/tauMinus;
		groupConfigMap[grpId].GAMMA				= gamma;
		groupConfigMap[grpId].KAPPA				= (1 + exp(-gamma/tauPlus))/(1 - exp(-gamma/tauPlus));
		groupConfigMap[grpId].OMEGA				= alphaPlus * (1 - groupConfigMap[grpId].KAPPA);
		// set flags for STDP function
		groupConfigMap[grpId].WithESTDPtype	= type;
		groupConfigMap[grpId].WithESTDPcurve = curve;
		groupConfigMap[grpId].WithESTDP		= isSet;
		groupConfigMap[grpId].WithSTDP		|= groupConfigMap[grpId].WithESTDP;
		sim_with_stdp					|= groupConfigMap[grpId].WithSTDP;

		KERNEL_INFO("E-STDP %s for %s(%d)", isSet?"enabled":"disabled", groupInfo[grpId].Name.c_str(), grpId);
	}
}

// set ISTDP params
void SNN::setISTDP(int grpId, bool isSet, STDPType type, STDPCurve curve, float ab1, float ab2, float tau1, float tau2) {
	assert(grpId>=-1);
	if (isSet) {
		assert(type!=UNKNOWN_STDP);
		assert(tau1>0); assert(tau2>0);
	}

	if (grpId==ALL) { // shortcut for all groups
		for(int grpId1=0; grpId1 < numGroups; grpId1++) {
			setISTDP(grpId1, isSet, type, curve, ab1, ab2, tau1, tau2);
		}
	} else {
		// set STDP for a given group
		// set params for STDP curve
		if (curve == EXP_CURVE) {
			groupConfigMap[grpId].ALPHA_PLUS_INB = ab1;
			groupConfigMap[grpId].ALPHA_MINUS_INB = ab2;
			groupConfigMap[grpId].TAU_PLUS_INV_INB = 1.0f / tau1;
			groupConfigMap[grpId].TAU_MINUS_INV_INB = 1.0f / tau2;
			groupConfigMap[grpId].BETA_LTP 		= 0.0f;
			groupConfigMap[grpId].BETA_LTD 		= 0.0f;
			groupConfigMap[grpId].LAMBDA			= 1.0f;
			groupConfigMap[grpId].DELTA			= 1.0f;
		} else {
			groupConfigMap[grpId].ALPHA_PLUS_INB = 0.0f;
			groupConfigMap[grpId].ALPHA_MINUS_INB = 0.0f;
			groupConfigMap[grpId].TAU_PLUS_INV_INB = 1.0f;
			groupConfigMap[grpId].TAU_MINUS_INV_INB = 1.0f;
			groupConfigMap[grpId].BETA_LTP 		= ab1;
			groupConfigMap[grpId].BETA_LTD 		= ab2;
			groupConfigMap[grpId].LAMBDA			= tau1;
			groupConfigMap[grpId].DELTA			= tau2;
		}
		// set flags for STDP function
		//FIXME: separate STDPType to ESTDPType and ISTDPType
		groupConfigMap[grpId].WithISTDPtype	= type;
		groupConfigMap[grpId].WithISTDPcurve = curve;
		groupConfigMap[grpId].WithISTDP		= isSet;
		groupConfigMap[grpId].WithSTDP		|= groupConfigMap[grpId].WithISTDP;
		sim_with_stdp					|= groupConfigMap[grpId].WithSTDP;

		KERNEL_INFO("I-STDP %s for %s(%d)", isSet?"enabled":"disabled", groupInfo[grpId].Name.c_str(), grpId);
	}
}

// set STP params
void SNN::setSTP(int grpId, bool isSet, float STP_U, float STP_tau_u, float STP_tau_x) {
	assert(grpId>=-1);
	if (isSet) {
		assert(STP_U>0 && STP_U<=1); assert(STP_tau_u>0); assert(STP_tau_x>0);
	}

	if (grpId == ALL) { // shortcut for all groups
		for(int grpId1=0; grpId1<numGroups; grpId1++) {
			setSTP(grpId1, isSet, STP_U, STP_tau_u, STP_tau_x);
		}
	} else {
		// set STDP for a given group
		sim_with_stp 				   |= isSet;
		groupConfigMap[grpId].WithSTP 		= isSet;
		groupConfigMap[grpId].STP_A 			= (STP_U>0.0f) ? 1.0/STP_U : 1.0f; // scaling factor
		groupConfigMap[grpId].STP_U 			= STP_U;
		groupConfigMap[grpId].STP_tau_u_inv	= 1.0f/STP_tau_u; // facilitatory
		groupConfigMap[grpId].STP_tau_x_inv	= 1.0f/STP_tau_x; // depressive
		groupConfigMap[grpId].newUpdates = true;

		KERNEL_INFO("STP %s for %d (%s):\tA: %1.4f, U: %1.4f, tau_u: %4.0f, tau_x: %4.0f", isSet?"enabled":"disabled",
					grpId, groupInfo[grpId].Name.c_str(), groupConfigMap[grpId].STP_A, STP_U, STP_tau_u, STP_tau_x);
	}
}

void SNN::setWeightAndWeightChangeUpdate(UpdateInterval wtANDwtChangeUpdateInterval, bool enableWtChangeDecay, float wtChangeDecay) {
	assert(wtChangeDecay > 0.0f && wtChangeDecay < 1.0f);

	switch (wtANDwtChangeUpdateInterval) {
		case INTERVAL_10MS:
			wtANDwtChangeUpdateInterval_ = 10;
			break;
		case INTERVAL_100MS:
			wtANDwtChangeUpdateInterval_ = 100;
			break;
		case INTERVAL_1000MS:
		default:
			wtANDwtChangeUpdateInterval_ = 1000;
			break;
	}

	if (enableWtChangeDecay) {
		// set up stdp factor according to update interval
		switch (wtANDwtChangeUpdateInterval) {
		case INTERVAL_10MS:
			stdpScaleFactor_ = 0.005f;
			break;
		case INTERVAL_100MS:
			stdpScaleFactor_ = 0.05f;
			break;
		case INTERVAL_1000MS:
		default:
			stdpScaleFactor_ = 0.5f;
			break;
		}
		// set up weight decay
		wtChangeDecay_ = wtChangeDecay;
	} else {
		stdpScaleFactor_ = 1.0f;
		wtChangeDecay_ = 0.0f;
	}

	KERNEL_INFO("Update weight and weight change every %d ms", wtANDwtChangeUpdateInterval_);
	KERNEL_INFO("Weight Change Decay is %s", enableWtChangeDecay? "enabled" : "disable");
	KERNEL_INFO("STDP scale factor = %1.3f, wtChangeDecay = %1.3f", stdpScaleFactor_, wtChangeDecay_);
}

/// ************************************************************************************************************ ///
/// PUBLIC METHODS: GENERATE A SIMULATION
/// ************************************************************************************************************ ///

// reorganize the network and do the necessary allocation
// of all variable for carrying out the simulation..
// this code is run only one time during network initialization
void SNN::setupNetwork() {
	switch (snnState) {
	case CONFIG_SNN:
		compileSNN();
	case COMPILED_SNN:
		partitionSNN();
	case PARTITIONED_SNN:
		generateRuntimeSNN();
		break;
	case EXECUTABLE_SNN:
		break;
	default:
		KERNEL_ERROR("Unknown SNN state");
		break;
	}
}

/// ************************************************************************************************************ ///
/// PUBLIC METHODS: RUNNING A SIMULATION
/// ************************************************************************************************************ ///

int SNN::runNetwork(int _nsec, int _nmsec, bool printRunSummary, bool copyState) {
	assert(_nmsec >= 0 && _nmsec < 1000);
	assert(_nsec  >= 0);
	int runDurationMs = _nsec*1000 + _nmsec;
	KERNEL_DEBUG("runNetwork: runDur=%dms, printRunSummary=%s, copyState=%s", runDurationMs, printRunSummary?"y":"n",
		copyState?"y":"n");

	// setupNetwork() must have already been called
	assert(snnState == EXECUTABLE_SNN);

	// don't bother printing if logger mode is SILENT
	printRunSummary = (loggerMode_==SILENT) ? false : printRunSummary;

	// first-time run: inform the user the simulation is running now
	if (simTime==0 && printRunSummary) {
		KERNEL_INFO("");
		if (simMode_==GPU_MODE) {
			KERNEL_INFO("******************** Running GPU Simulation on %d GPU(s) ***************************", numGPUs_);
		} else {
			KERNEL_INFO("********************      Running CPU Simulation      ***************************");
		}
		KERNEL_INFO("");
	}

	// reset all spike counters
	if (simMode_==GPU_MODE)
		resetSpikeCnt_GPU(ALL);
	else
		resetSpikeCnt(ALL);

	// store current start time for future reference
	simTimeRunStart = simTime;
	simTimeRunStop  = simTime + runDurationMs;
	assert(simTimeRunStop >= simTimeRunStart); // check for arithmetic underflow

	// ConnectionMonitor is a special case: we might want the first snapshot at t=0 in the binary
	// but updateTime() is false for simTime==0.
	// And we cannot put this code in ConnectionMonitorCore::init, because then the user would have no
	// way to call ConnectionMonitor::setUpdateTimeIntervalSec before...
	if (simTime == 0 && numConnectionMonitor) {
		updateConnectionMonitor();
	}

	// set the Poisson generation time slice to be at the run duration up to 1000 ms.
	// \TODO: should it be PROPAGATED_BUFFER_SIZE-1 or PROPAGATED_BUFFER_SIZE ?
	setGrpTimeSlice(ALL, MAX(1, MIN(runDurationMs, 1000)));

	CUDA_RESET_TIMER(timer);
	CUDA_START_TIMER(timer);

	// if nsec=0, simTimeMs=10, we need to run the simulator for 10 timeStep;
	// if nsec=1, simTimeMs=10, we need to run the simulator for 1*1000+10, time Step;
	for(int i=0; i<runDurationMs; i++) {
		if(simMode_ == CPU_MODE)
			doSnnSim();
		else
			doGPUSim();

		// update weight every updateInterval ms if plastic synapses present
		if (!sim_with_fixedwts && wtANDwtChangeUpdateInterval_ == ++wtANDwtChangeUpdateIntervalCnt_) {
			wtANDwtChangeUpdateIntervalCnt_ = 0; // reset counter
			if (!sim_in_testing) {
				// keep this if statement separate from the above, so that the counter is updated correctly
				if (simMode_ == CPU_MODE) {
					updateWeights();
				} else{
					updateWeights_GPU();
				}
			}
		}

		// Note: updateTime() advance simTime, simTimeMs, and simTimeSec accordingly
		if (updateTime()) {
			// finished one sec of simulation...
			if (numSpikeMonitor) {
				updateSpikeMonitor();
			}
			if (numGroupMonitor) {
				updateGroupMonitor();
			}
			if (numConnectionMonitor) {
				updateConnectionMonitor();
			}

			if(simMode_ == CPU_MODE)
				shiftSpikeTables();
			else
				shiftSpikeTables_GPU();
		}

		if(simMode_ == GPU_MODE){
			copyFiringStateFromGPU();
		}
	}

	// in GPU mode, copy info from device to host
	if (simMode_==GPU_MODE) {
		if(copyState) {
			copyNeuronState(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false, ALL);

			if (sim_with_stp) {
				copySTPState(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false);
			}
		}
	}

	// user can opt to display some runNetwork summary
	if (printRunSummary) {

		// if there are Monitors available and it's time to show the log, print status for each group
		if (numSpikeMonitor) {
			printStatusSpikeMonitor(ALL);
		}
		if (numConnectionMonitor) {
			printStatusConnectionMonitor(ALL);
		}
		if (numGroupMonitor) {
			printStatusGroupMonitor(ALL);
		}

		// record time of run summary print
		simTimeLastRunSummary = simTime;
	}

	// call updateSpike(Group)Monitor again to fetch all the left-over spikes and group status (neuromodulator)
	updateSpikeMonitor();
	updateGroupMonitor();

	// keep track of simulation time...
	CUDA_STOP_TIMER(timer);
	lastExecutionTime = CUDA_GET_TIMER_VALUE(timer);
	cumExecutionTime += lastExecutionTime;
	return 0;
}



/// ************************************************************************************************************ ///
/// PUBLIC METHODS: INTERACTING WITH A SIMULATION
/// ************************************************************************************************************ ///

// adds a bias to every weight in the connection
void SNN::biasWeights(short int connId, float bias, bool updateWeightRange) {
	assert(connId>=0 && connId<numConnections);

	// iterate over all postsynaptic neurons
	for (int i=groupConfigs[0][connectConfigMap[connId].grpDest].StartN; i<=groupConfigs[0][connectConfigMap[connId].grpDest].EndN; i++) {
		unsigned int cumIdx = managerRuntimeData.cumulativePre[i];

		// iterate over all presynaptic neurons
		unsigned int pos_ij = cumIdx;
		for (int j=0; j<managerRuntimeData.Npre[i]; pos_ij++, j++) {
			if (managerRuntimeData.connIdsPreIdx[pos_ij]==connId) {
				// apply bias to weight
				float weight = managerRuntimeData.wt[pos_ij] + bias;

				// inform user of acton taken if weight is out of bounds
//				bool needToPrintDebug = (weight+bias>connInfo->maxWt || weight+bias<connInfo->minWt);
				bool needToPrintDebug = (weight > connectConfigMap[connId].maxWt || weight < 0.0f);

				if (updateWeightRange) {
					// if this flag is set, we need to update minWt,maxWt accordingly
					// will be saving new maxSynWt and copying to GPU below
//					connInfo->minWt = fmin(connInfo->minWt, weight);
					connectConfigMap[connId].maxWt = fmax(connectConfigMap[connId].maxWt, weight);
					if (needToPrintDebug) {
						KERNEL_DEBUG("biasWeights(%d,%f,%s): updated weight ranges to [%f,%f]", connId, bias,
							(updateWeightRange?"true":"false"), 0.0f, connectConfigMap[connId].maxWt);
					}
				} else {
					// constrain weight to boundary values
					// compared to above, we swap minWt/maxWt logic
					weight = fmin(weight, connectConfigMap[connId].maxWt);
//					weight = fmax(weight, connInfo->minWt);
					weight = fmax(weight, 0.0f);
					if (needToPrintDebug) {
						KERNEL_DEBUG("biasWeights(%d,%f,%s): constrained weight %f to [%f,%f]", connId, bias,
							(updateWeightRange?"true":"false"), weight, 0.0f, connectConfigMap[connId].maxWt);
					}
				}

				// update datastructures
				managerRuntimeData.wt[pos_ij] = weight;
				managerRuntimeData.maxSynWt[pos_ij] = connectConfigMap[connId].maxWt; // it's easier to just update, even if it hasn't changed
			}
		}

		// update GPU datastructures in batches, grouped by post-neuron
		if (simMode_==GPU_MODE) {
			CUDA_CHECK_ERRORS( cudaMemcpy(&(gpuRuntimeData[0].wt[cumIdx]), &(managerRuntimeData.wt[cumIdx]), sizeof(float)*managerRuntimeData.Npre[i],
				cudaMemcpyHostToDevice) );

			if (gpuRuntimeData[0].maxSynWt!=NULL) {
				// only copy maxSynWt if datastructure actually exists on the GPU
				// (that logic should be done elsewhere though)
				CUDA_CHECK_ERRORS( cudaMemcpy(&(gpuRuntimeData[0].maxSynWt[cumIdx]), &(managerRuntimeData.maxSynWt[cumIdx]),
					sizeof(float)*managerRuntimeData.Npre[i], cudaMemcpyHostToDevice) );
			}
		}
	}
}

// deallocates dynamical structures and exits
void SNN::exitSimulation(int val) {
	deleteObjects();
	exit(val);
}

// reads network state from file
void SNN::loadSimulation(FILE* fid) {
	loadSimFID = fid;
}

// reset spike counter to zero
void SNN::resetSpikeCounter(int grpId) {
	if (!sim_with_spikecounters)
		return;

	assert(grpId>=-1); assert(grpId<numGroups);

	if (grpId == ALL) { // shortcut for all groups
		for(int grpId1=0; grpId1<numGroups; grpId1 ++) {
			resetSpikeCounter(grpId1);
		}
	} else {
		// only update if SpikeMonRT is set for this group
		if (!groupConfigs[0][grpId].withSpikeCounter)
			return;

		groupConfigs[0][grpId].spkCntRecordDurHelper = 0;

		if (simMode_==GPU_MODE) {
			resetSpikeCounter_GPU(grpId);
		}
		else {
			int bufPos = groupConfigs[0][grpId].spkCntBufPos; // retrieve buf pos
			memset(spkCntBuf[bufPos],0,groupConfigs[0][grpId].SizeN*sizeof(int)); // set all to 0
		}
	}
}

// multiplies every weight with a scaling factor
void SNN::scaleWeights(short int connId, float scale, bool updateWeightRange) {
	assert(connId>=0 && connId<numConnections);
	assert(scale>=0.0f);

	// iterate over all postsynaptic neurons
	for (int i=groupConfigs[0][connectConfigMap[connId].grpDest].StartN; i<=groupConfigs[0][connectConfigMap[connId].grpDest].EndN; i++) {
		unsigned int cumIdx = managerRuntimeData.cumulativePre[i];

		// iterate over all presynaptic neurons
		unsigned int pos_ij = cumIdx;
		for (int j=0; j<managerRuntimeData.Npre[i]; pos_ij++, j++) {
			if (managerRuntimeData.connIdsPreIdx[pos_ij]==connId) {
				// apply bias to weight
				float weight = managerRuntimeData.wt[pos_ij]*scale;

				// inform user of acton taken if weight is out of bounds
//				bool needToPrintDebug = (weight>connInfo->maxWt || weight<connInfo->minWt);
				bool needToPrintDebug = (weight > connectConfigMap[connId].maxWt || weight < 0.0f);

				if (updateWeightRange) {
					// if this flag is set, we need to update minWt,maxWt accordingly
					// will be saving new maxSynWt and copying to GPU below
//					connInfo->minWt = fmin(connInfo->minWt, weight);
					connectConfigMap[connId].maxWt = fmax(connectConfigMap[connId].maxWt, weight);
					if (needToPrintDebug) {
						KERNEL_DEBUG("scaleWeights(%d,%f,%s): updated weight ranges to [%f,%f]", connId, scale,
							(updateWeightRange?"true":"false"), 0.0f, connectConfigMap[connId].maxWt);
					}
				} else {
					// constrain weight to boundary values
					// compared to above, we swap minWt/maxWt logic
					weight = fmin(weight, connectConfigMap[connId].maxWt);
//					weight = fmax(weight, connInfo->minWt);
					weight = fmax(weight, 0.0f);
					if (needToPrintDebug) {
						KERNEL_DEBUG("scaleWeights(%d,%f,%s): constrained weight %f to [%f,%f]", connId, scale,
							(updateWeightRange?"true":"false"), weight, 0.0f, connectConfigMap[connId].maxWt);
					}
				}

				// update datastructures
				managerRuntimeData.wt[pos_ij] = weight;
				managerRuntimeData.maxSynWt[pos_ij] = connectConfigMap[connId].maxWt; // it's easier to just update, even if it hasn't changed
			}
		}

		// update GPU datastructures in batches, grouped by post-neuron
		if (simMode_==GPU_MODE) {
			CUDA_CHECK_ERRORS( cudaMemcpy(&(gpuRuntimeData[0].wt[cumIdx]), &(managerRuntimeData.wt[cumIdx]), sizeof(float)*managerRuntimeData.Npre[i],
				cudaMemcpyHostToDevice) );

			if (gpuRuntimeData[0].maxSynWt!=NULL) {
				// only copy maxSynWt if datastructure actually exists on the GPU
				// (that logic should be done elsewhere though)
				CUDA_CHECK_ERRORS( cudaMemcpy(&(gpuRuntimeData[0].maxSynWt[cumIdx]), &(managerRuntimeData.maxSynWt[cumIdx]),
					sizeof(float)*managerRuntimeData.Npre[i], cudaMemcpyHostToDevice));
			}
		}
	}
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, where groupConfigs[0][] might not be available
// or groupConfigMap is not sync with groupConfigs[0][]
GroupMonitor* SNN::setGroupMonitor(int grpId, FILE* fid) {
	// check whether group already has a GroupMonitor
	if (groupConfigs[0][grpId].GroupMonitorId >= 0) {
		KERNEL_ERROR("setGroupMonitor has already been called on Group %d (%s).",
			grpId, groupInfo[grpId].Name.c_str());
		exitSimulation(1);
	}

	// create new GroupMonitorCore object in any case and initialize analysis components
	// grpMonObj destructor (see below) will deallocate it
	GroupMonitorCore* grpMonCoreObj = new GroupMonitorCore(this, numGroupMonitor, grpId);
	groupMonCoreList[numGroupMonitor] = grpMonCoreObj;

	// assign group status file ID if we selected to write to a file, else it's NULL
	// if file pointer exists, it has already been fopened
	// this will also write the header section of the group status file
	// grpMonCoreObj destructor will fclose it
	grpMonCoreObj->setGroupFileId(fid);

	// create a new GroupMonitor object for the user-interface
	// SNN::deleteObjects will deallocate it
	GroupMonitor* grpMonObj = new GroupMonitor(grpMonCoreObj);
	groupMonList[numGroupMonitor] = grpMonObj;

	// also inform the group that it is being monitored...
	groupConfigs[0][grpId].GroupMonitorId = numGroupMonitor;

    // not eating much memory anymore, got rid of all buffers
	cpuSnnSz.monitorInfoSize += sizeof(GroupMonitor*);
	cpuSnnSz.monitorInfoSize += sizeof(GroupMonitorCore*);

	numGroupMonitor++;
	KERNEL_INFO("GroupMonitor set for group %d (%s)",grpId,groupInfo[grpId].Name.c_str());

	return grpMonObj;
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, where group(connect)Config[] might not be available
// or group(connect)ConfigMap is not sync with group(connect)Config[]
ConnectionMonitor* SNN::setConnectionMonitor(int grpIdPre, int grpIdPost, FILE* fid) {
	// find connection based on pre-post pair
	short int connId = getConnectId(grpIdPre, grpIdPost);
	if (connId<0) {
		KERNEL_ERROR("No connection found from group %d(%s) to group %d(%s)", grpIdPre, getGroupName(grpIdPre).c_str(),
			grpIdPost, getGroupName(grpIdPost).c_str());
		exitSimulation(1);
	}

	// check whether connection already has a connection monitor
	if (connectConfigMap[connId].connectionMonitorId >= 0) {
		KERNEL_ERROR("setConnectionMonitor has already been called on Connection %d (MonitorId=%d)", connId, connectConfigMap[connId].connectionMonitorId);
		exitSimulation(1);
	}

	// inform the connection that it is being monitored...
	// this needs to be called before new ConnectionMonitorCore
	connectConfigMap[connId].connectionMonitorId = numConnectionMonitor;

	// create new ConnectionMonitorCore object in any case and initialize
	// connMonObj destructor (see below) will deallocate it
	ConnectionMonitorCore* connMonCoreObj = new ConnectionMonitorCore(this, numConnectionMonitor, connId,
		grpIdPre, grpIdPost);
	connMonCoreList[numConnectionMonitor] = connMonCoreObj;

	// assign conn file ID if we selected to write to a file, else it's NULL
	// if file pointer exists, it has already been fopened
	// this will also write the header section of the conn file
	// connMonCoreObj destructor will fclose it
	connMonCoreObj->setConnectFileId(fid);

	// create a new ConnectionMonitor object for the user-interface
	// SNN::deleteObjects will deallocate it
	ConnectionMonitor* connMonObj = new ConnectionMonitor(connMonCoreObj);
	connMonList[numConnectionMonitor] = connMonObj;

	// now init core object (depends on several datastructures allocated above)
	connMonCoreObj->init();

    // not eating much memory anymore, got rid of all buffers
	cpuSnnSz.monitorInfoSize += sizeof(ConnectionMonitor*);
	cpuSnnSz.monitorInfoSize += sizeof(ConnectionMonitorCore*);

	numConnectionMonitor++;
	KERNEL_INFO("ConnectionMonitor %d set for Connection %d: %d(%s) => %d(%s)", connectConfigMap[connId].connectionMonitorId, connId, grpIdPre, getGroupName(grpIdPre).c_str(),
		grpIdPost, getGroupName(grpIdPost).c_str());

	return connMonObj;
}

void SNN::setExternalCurrent(int grpId, const std::vector<float>& current) {
	assert(grpId>=0); assert(grpId<numGroups);
	assert(!isPoissonGroup(grpId));
	assert(current.size() == getGroupNumNeurons(grpId));

	// // update flag for faster handling at run-time
	// if (count_if(current.begin(), current.end(), isGreaterThanZero)) {
	// 	groupConfigs[0][grpId].WithCurrentInjection = true;
	// } else {
	// 	groupConfigs[0][grpId].WithCurrentInjection = false;
	// }

	// store external current in array
	for (int i=groupConfigs[0][grpId].StartN, j=0; i<=groupConfigs[0][grpId].EndN; i++, j++) {
		managerRuntimeData.extCurrent[i] = current[j];
	}

	// copy to GPU if necessary
	// don't allocate; allocation done in generateRuntimeData
	if (simMode_==GPU_MODE) {
		copyExternalCurrent(&gpuRuntimeData[0], &managerRuntimeData, false, grpId);
	}
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, where groupConfigs[0][] might not be available
// or groupConfigMap is not sync with groupConfigs[0][]
// sets up a spike generator
void SNN::setSpikeGenerator(int grpId, SpikeGeneratorCore* spikeGen) {
	assert(snnState == CONFIG_SNN); // must be called before setupNetwork() to work on GPU
	assert(spikeGen);
	assert (groupConfigMap[grpId].isSpikeGenerator);
	groupConfigMap[grpId].spikeGen = spikeGen;
}

// A Spike Counter keeps track of the number of spikes per neuron in a group.
void SNN::setSpikeCounter(int grpId, int recordDur) {
	assert(grpId>=0); assert(grpId<numGroups);

	sim_with_spikecounters = true; // inform simulation
	groupConfigMap[grpId].withSpikeCounter = true; // inform the group
	groupConfigMap[grpId].spkCntRecordDur = (recordDur>0)?recordDur:-1; // set record duration, after which spike buf will be reset
	groupConfigMap[grpId].spkCntRecordDurHelper = 0; // counter to help make fast modulo
	groupConfigMap[grpId].spkCntBufPos = numSpkCnt; // inform group which pos it has in spike buf
	spkCntBuf[numSpkCnt] = new int[groupConfigMap[grpId].SizeN]; // create spike buf
	memset(spkCntBuf[numSpkCnt],0,(groupConfigMap[grpId].SizeN)*sizeof(int)); // set all to 0

	numSpkCnt++;

	KERNEL_INFO("SpikeCounter set for Group %d (%s): %d ms recording window", grpId, groupInfo[grpId].Name.c_str(),
		recordDur);
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, where groupConfigs[0][] might not be available
// or groupConfigMap is not sync with groupConfigs[0][]
// record spike information, return a SpikeInfo object
SpikeMonitor* SNN::setSpikeMonitor(int grpId, FILE* fid) {
	// check whether group already has a SpikeMonitor
	if (groupConfigs[0][grpId].SpikeMonitorId >= 0) {
		// in this case, return the current object and update fid
		SpikeMonitor* spkMonObj = getSpikeMonitor(grpId);

		// update spike file ID
		SpikeMonitorCore* spkMonCoreObj = getSpikeMonitorCore(grpId);
		spkMonCoreObj->setSpikeFileId(fid);

		KERNEL_INFO("SpikeMonitor updated for group %d (%s)",grpId,groupInfo[grpId].Name.c_str());
		return spkMonObj;
	} else {
		// create new SpikeMonitorCore object in any case and initialize analysis components
		// spkMonObj destructor (see below) will deallocate it
		SpikeMonitorCore* spkMonCoreObj = new SpikeMonitorCore(this, numSpikeMonitor, grpId);
		spikeMonCoreList[numSpikeMonitor] = spkMonCoreObj;

		// assign spike file ID if we selected to write to a file, else it's NULL
		// if file pointer exists, it has already been fopened
		// this will also write the header section of the spike file
		// spkMonCoreObj destructor will fclose it
		spkMonCoreObj->setSpikeFileId(fid);

		// create a new SpikeMonitor object for the user-interface
		// SNN::deleteObjects will deallocate it
		SpikeMonitor* spkMonObj = new SpikeMonitor(spkMonCoreObj);
		spikeMonList[numSpikeMonitor] = spkMonObj;

		// also inform the grp that it is being monitored...
		groupConfigs[0][grpId].SpikeMonitorId	= numSpikeMonitor;

    	// not eating much memory anymore, got rid of all buffers
		cpuSnnSz.monitorInfoSize += sizeof(SpikeMonitor*);
		cpuSnnSz.monitorInfoSize += sizeof(SpikeMonitorCore*);

		numSpikeMonitor++;
		KERNEL_INFO("SpikeMonitor set for group %d (%s)",grpId,groupInfo[grpId].Name.c_str());

		return spkMonObj;
	}
}

// FIXME: distinguish the function call at CONFIG_STATE and RUN_STATE, where groupConfigs[0][] might not be available
// or groupConfigMap is not sync with groupConfigs[0][]
// assigns spike rate to group
void SNN::setSpikeRate(int grpId, PoissonRate* ratePtr, int refPeriod) {
	assert(grpId>=0 && grpId<numGroups);
	assert(ratePtr);
	assert(groupConfigs[0][grpId].isSpikeGenerator);
	assert(ratePtr->getNumNeurons()==groupConfigs[0][grpId].SizeN);
	assert(refPeriod>=1);

	groupConfigs[0][grpId].RatePtr = ratePtr;
	groupConfigs[0][grpId].RefractPeriod   = refPeriod;
	spikeRateUpdated = true;
}

// sets the weight value of a specific synapse
void SNN::setWeight(short int connId, int neurIdPre, int neurIdPost, float weight, bool updateWeightRange) {
	assert(connId>=0 && connId<getNumConnections());
	assert(weight>=0.0f);

	assert(neurIdPre >= 0  && neurIdPre < getGroupNumNeurons(connectConfigMap[connId].grpSrc));
	assert(neurIdPost >= 0 && neurIdPost < getGroupNumNeurons(connectConfigMap[connId].grpDest));

	float maxWt = fabs(connectConfigMap[connId].maxWt);
	float minWt = 0.0f;

	// inform user of acton taken if weight is out of bounds
	bool needToPrintDebug = (weight>maxWt || weight<minWt);

	if (updateWeightRange) {
		// if this flag is set, we need to update minWt,maxWt accordingly
		// will be saving new maxSynWt and copying to GPU below
//		connInfo->minWt = fmin(connInfo->minWt, weight);
		maxWt = fmax(maxWt, weight);
		if (needToPrintDebug) {
			KERNEL_DEBUG("setWeight(%d,%d,%d,%f,%s): updated weight ranges to [%f,%f]", connId, neurIdPre, neurIdPost,
				weight, (updateWeightRange?"true":"false"), minWt, maxWt);
		}
	} else {
		// constrain weight to boundary values
		// compared to above, we swap minWt/maxWt logic
		weight = fmin(weight, maxWt);
		weight = fmax(weight, minWt);
		if (needToPrintDebug) {
			KERNEL_DEBUG("setWeight(%d,%d,%d,%f,%s): constrained weight %f to [%f,%f]", connId, neurIdPre, neurIdPost,
				weight, (updateWeightRange?"true":"false"), weight, minWt, maxWt);
		}
	}

	// find real ID of pre- and post-neuron
	int neurIdPreReal = groupConfigs[0][connectConfigMap[connId].grpSrc].StartN+neurIdPre;
	int neurIdPostReal = groupConfigs[0][connectConfigMap[connId].grpDest].StartN+neurIdPost;

	// iterate over all presynaptic synapses until right one is found
	bool synapseFound = false;
	int pos_ij = managerRuntimeData.cumulativePre[neurIdPostReal];
	for (int j=0; j<managerRuntimeData.Npre[neurIdPostReal]; pos_ij++, j++) {
		SynInfo* preId = &(managerRuntimeData.preSynapticIds[pos_ij]);
		int pre_nid = GET_CONN_NEURON_ID((*preId));
		if (GET_CONN_NEURON_ID((*preId))==neurIdPreReal) {
			assert(managerRuntimeData.connIdsPreIdx[pos_ij]==connId); // make sure we've got the right connection ID

			managerRuntimeData.wt[pos_ij] = isExcitatoryGroup(connectConfigMap[connId].grpSrc) ? weight : -1.0*weight;
			managerRuntimeData.maxSynWt[pos_ij] = isExcitatoryGroup(connectConfigMap[connId].grpSrc) ? maxWt : -1.0*maxWt;

			if (simMode_==GPU_MODE) {
				// need to update datastructures on GPU
				CUDA_CHECK_ERRORS( cudaMemcpy(&(gpuRuntimeData[0].wt[pos_ij]), &(managerRuntimeData.wt[pos_ij]), sizeof(float), cudaMemcpyHostToDevice));
				if (gpuRuntimeData[0].maxSynWt!=NULL) {
					// only copy maxSynWt if datastructure actually exists on the GPU
					// (that logic should be done elsewhere though)
					CUDA_CHECK_ERRORS( cudaMemcpy(&(gpuRuntimeData[0].maxSynWt[pos_ij]), &(managerRuntimeData.maxSynWt[pos_ij]), sizeof(float), cudaMemcpyHostToDevice));
				}
			}

			// synapse found and updated: we're done!
			synapseFound = true;
			break;
		}
	}

	if (!synapseFound) {
		KERNEL_WARN("setWeight(%d,%d,%d,%f,%s): Synapse does not exist, not updated.", connId, neurIdPre, neurIdPost,
			weight, (updateWeightRange?"true":"false"));
	}
}

// writes network state to file
// handling of file pointer should be handled externally: as far as this function is concerned, it is simply
// trying to write to file
void SNN::saveSimulation(FILE* fid, bool saveSynapseInfo) {
	//int tmpInt;
	//float tmpFloat;

	//// +++++ WRITE HEADER SECTION +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ //

	//// write file signature
	//tmpInt = 294338571; // some int used to identify saveSimulation files
	//if (!fwrite(&tmpInt,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//// write version number
	//tmpFloat = 0.2f;
	//if (!fwrite(&tmpFloat,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//// write simulation time so far (in seconds)
	//tmpFloat = ((float)simTimeSec) + ((float)simTimeMs)/1000.0f;
	//if (!fwrite(&tmpFloat,sizeof(float),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//// write execution time so far (in seconds)
	//if(simMode_ == GPU_MODE) {
	//	stopGPUTiming();
	//	tmpFloat = gpuExecutionTime/1000.0f;
	//} else {
	//	stopCPUTiming();
	//	tmpFloat = cpuExecutionTime/1000.0f;
	//}
	//if (!fwrite(&tmpFloat,sizeof(float),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//// TODO: add more params of interest

	//// write network info
	//if (!fwrite(&numN,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//if (!fwrite(&numPreSynNet,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//if (!fwrite(&numPostSynNet,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//if (!fwrite(&numGroups,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//// write group info
	//char name[100];
	//for (int g=0;g<numGroups;g++) {
	//	if (!fwrite(&groupConfigs[0][g].StartN,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//	if (!fwrite(&groupConfigs[0][g].EndN,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//	if (!fwrite(&groupConfigs[0][g].SizeX,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//	if (!fwrite(&groupConfigs[0][g].SizeY,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//	if (!fwrite(&groupConfigs[0][g].SizeZ,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//	strncpy(name,groupInfo[g].Name.c_str(),100);
	//	if (!fwrite(name,1,100,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//}

	//// +++++ Fetch WEIGHT DATA (GPU Mode only) ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ //
	//if (simMode_ == GPU_MODE)
	//	copyWeightState(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false);
	//// +++++ WRITE SYNAPSE INFO +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ //

	//// \FIXME: replace with faster version
	//if (saveSynapseInfo) {
	//	for (int i = 0; i < numN; i++) {
	//		unsigned int offset = managerRuntimeData.cumulativePost[i];

	//		unsigned int count = 0;
	//		for (int t=0;t<maxDelay_;t++) {
	//			delay_info_t dPar = managerRuntimeData.postDelayInfo[i*(maxDelay_+1)+t];

	//			for(int idx_d=dPar.delay_index_start; idx_d<(dPar.delay_index_start+dPar.delay_length); idx_d++)
	//				count++;
	//		}

	//		if (!fwrite(&count,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");

	//		for (int t=0;t<maxDelay_;t++) {
	//			delay_info_t dPar = managerRuntimeData.postDelayInfo[i*(maxDelay_+1)+t];

	//			for(int idx_d=dPar.delay_index_start; idx_d<(dPar.delay_index_start+dPar.delay_length); idx_d++) {
	//				// get synaptic info...
	//				SynInfo post_info = managerRuntimeData.postSynapticIds[offset + idx_d];

	//				// get neuron id
	//				//int p_i = (post_info&POST_SYN_NEURON_MASK);
	//				unsigned int p_i = GET_CONN_NEURON_ID(post_info);
	//				assert(p_i<numN);

	//				// get syn id
	//				unsigned int s_i = GET_CONN_SYN_ID(post_info);
	//				//>>POST_SYN_NEURON_BITS)&POST_SYN_CONN_MASK;
	//				assert(s_i<(managerRuntimeData.Npre[p_i]));

	//				// get the cumulative position for quick access...
	//				unsigned int pos_i = managerRuntimeData.cumulativePre[p_i] + s_i;

	//				uint8_t delay = t+1;
	//				uint8_t plastic = s_i < managerRuntimeData.Npre_plastic[p_i]; // plastic or fixed.

	//				if (!fwrite(&i,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//				if (!fwrite(&p_i,sizeof(int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//				if (!fwrite(&(managerRuntimeData.wt[pos_i]),sizeof(float),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//				if (!fwrite(&(managerRuntimeData.maxSynWt[pos_i]),sizeof(float),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//				if (!fwrite(&delay,sizeof(uint8_t),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//				if (!fwrite(&plastic,sizeof(uint8_t),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//				if (!fwrite(&(managerRuntimeData.connIdsPreIdx[pos_i]),sizeof(short int),1,fid)) KERNEL_ERROR("saveSimulation fwrite error");
	//			}
	//		}
	//	}
	//}
}

// writes population weights from gIDpre to gIDpost to file fname in binary
void SNN::writePopWeights(std::string fname, int grpIdPre, int grpIdPost) {
	assert(grpIdPre>=0); assert(grpIdPost>=0);

	float* weights;
	int matrixSize;
	FILE* fid;
	int numPre, numPost;
	fid = fopen(fname.c_str(), "wb");
	assert(fid != NULL);

	if(snnState == CONFIG_SNN || snnState == COMPILED_SNN || snnState == PARTITIONED_SNN){
		KERNEL_ERROR("Simulation has not been run yet, cannot output weights.");
		exitSimulation(1);
	}

	SynInfo* preId;
	int pre_nid, pos_ij;

	//population sizes
	numPre = groupConfigs[0][grpIdPre].SizeN;
	numPost = groupConfigs[0][grpIdPost].SizeN;

	//first iteration gets the number of synaptic weights to place in our
	//weight matrix.
	matrixSize=0;
	//iterate over all neurons in the post group
	for (int i=groupConfigs[0][grpIdPost].StartN; i<=groupConfigs[0][grpIdPost].EndN; i++) {
		// for every post-neuron, find all pre
		pos_ij = managerRuntimeData.cumulativePre[i]; // i-th neuron, j=0th synapse
		//iterate over all presynaptic synapses
		for(int j=0; j<managerRuntimeData.Npre[i]; pos_ij++,j++) {
			preId = &managerRuntimeData.preSynapticIds[pos_ij];
			pre_nid = GET_CONN_NEURON_ID((*preId)); // neuron id of pre
			if (pre_nid<groupConfigs[0][grpIdPre].StartN || pre_nid>groupConfigs[0][grpIdPre].EndN)
				continue; // connection does not belong to group grpIdPre
			matrixSize++;
		}
	}

	//now we have the correct size
	weights = new float[matrixSize];
	//second iteration assigns the weights
	int curr = 0; // iterator for return array
	//iterate over all neurons in the post group
	for (int i=groupConfigs[0][grpIdPost].StartN; i<=groupConfigs[0][grpIdPost].EndN; i++) {
		// for every post-neuron, find all pre
		pos_ij = managerRuntimeData.cumulativePre[i]; // i-th neuron, j=0th synapse
		//do the GPU copy here.  Copy the current weights from GPU to CPU.
		if(simMode_==GPU_MODE){
			copyWeightsGPU(i,grpIdPre);
		}
		//iterate over all presynaptic synapses
		for(int j=0; j<managerRuntimeData.Npre[i]; pos_ij++,j++) {
			preId = &(managerRuntimeData.preSynapticIds[pos_ij]);
			pre_nid = GET_CONN_NEURON_ID((*preId)); // neuron id of pre
			if (pre_nid<groupConfigs[0][grpIdPre].StartN || pre_nid>groupConfigs[0][grpIdPre].EndN)
				continue; // connection does not belong to group grpIdPre
			weights[curr] = managerRuntimeData.wt[pos_ij];
			curr++;
		}
	}

	fwrite(weights,sizeof(float),matrixSize,fid);
	fclose(fid);
	//Let my memory FREE!!!
	delete [] weights;
}


/// ************************************************************************************************************ ///
/// PUBLIC METHODS: PLOTTING / LOGGING
/// ************************************************************************************************************ ///

// set new file pointer for all files
// fp==NULL is code for don't change it
// can be called in all logger modes; however, the analogous interface function can only be called in CUSTOM
void SNN::setLogsFp(FILE* fpInf, FILE* fpErr, FILE* fpDeb, FILE* fpLog) {
	if (fpInf!=NULL) {
		if (fpInf_!=NULL && fpInf_!=stdout && fpInf_!=stderr)
			fclose(fpInf_);
		fpInf_ = fpInf;
	}

	if (fpErr!=NULL) {
		if (fpErr_ != NULL && fpErr_!=stdout && fpErr_!=stderr)
			fclose(fpErr_);
		fpErr_ = fpErr;
	}

	if (fpDeb!=NULL) {
		if (fpDeb_!=NULL && fpDeb_!=stdout && fpDeb_!=stderr)
			fclose(fpDeb_);
		fpDeb_ = fpDeb;
	}

	if (fpLog!=NULL) {
		if (fpLog_!=NULL && fpLog_!=stdout && fpLog_!=stderr)
			fclose(fpLog_);
		fpLog_ = fpLog;
	}
}


/// **************************************************************************************************************** ///
/// GETTERS / SETTERS
/// **************************************************************************************************************** ///

// loop over linked list entries to find a connection with the right pre-post pair, O(N)
short int SNN::getConnectId(int grpIdPre, int grpIdPost) {
	short int connId = -1;

	for (std::map<int, ConnectConfig>::iterator it = connectConfigMap.begin(); it != connectConfigMap.end(); it++) {
		if (it->second.grpSrc == grpIdPre && it->second.grpDest == grpIdPost) {
			connId = it->second.connId;
			break;
		}
	}

	return connId;
}

//! used for parameter tuning functionality
//ConnectConfig* SNN::getConnectInfo(short int connectId) {
//	ConnectConfig* nextConn = connectBegin;
//	CHECK_CONNECTION_ID(connectId, numConnections);
//
//	// clear all existing connection info...
//	while (nextConn) {
//		if (nextConn->connId == connectId) {
//			nextConn->newUpdates = true;		// \FIXME: this is a Jay hack
//			return nextConn;
//		}
//		nextConn = nextConn->next;
//	}
//
//	KERNEL_DEBUG("Total Connections = %d", numConnections);
//	KERNEL_DEBUG("ConnectId (%d) cannot be recognized", connectId);
//	return NULL;
//}

ConnectConfig SNN::getConnectConfig(short int connId) {
	CHECK_CONNECTION_ID(connId, numConnections);

	if (connectConfigMap.find(connId) == connectConfigMap.end()) {
		KERNEL_ERROR("Total Connections = %d", numConnections);
		KERNEL_ERROR("ConnectId (%d) cannot be recognized", connId);
	}
	
	connectConfigMap[connId].newUpdates = true;// \FIXME: this is a Jay hack

	return connectConfigMap[connId];
}

std::vector<float> SNN::getConductanceAMPA(int grpId) {
	assert(isSimulationWithCOBA());

	// need to copy data from GPU first
	if (getSimMode()==GPU_MODE) {
		copyConductanceAMPA(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false, grpId);
	}

	std::vector<float> gAMPAvec;
	for (int i=groupConfigs[0][grpId].StartN; i<=groupConfigs[0][grpId].EndN; i++) {
		gAMPAvec.push_back(managerRuntimeData.gAMPA[i]);
	}
	return gAMPAvec;
}

std::vector<float> SNN::getConductanceNMDA(int grpId) {
	assert(isSimulationWithCOBA());

	// need to copy data from GPU first
	if (getSimMode()==GPU_MODE)
		copyConductanceNMDA(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false, grpId);

	std::vector<float> gNMDAvec;
	if (isSimulationWithNMDARise()) {
		// need to construct conductance from rise and decay parts
		for (int i=groupConfigs[0][grpId].StartN; i<=groupConfigs[0][grpId].EndN; i++) {
			gNMDAvec.push_back(managerRuntimeData.gNMDA_d[i]-managerRuntimeData.gNMDA_r[i]);
		}
	} else {
		for (int i=groupConfigs[0][grpId].StartN; i<=groupConfigs[0][grpId].EndN; i++) {
			gNMDAvec.push_back(managerRuntimeData.gNMDA[i]);
		}
	}
	return gNMDAvec;
}

std::vector<float> SNN::getConductanceGABAa(int grpId) {
	assert(isSimulationWithCOBA());

	// need to copy data from GPU first
	if (getSimMode()==GPU_MODE) {
		copyConductanceGABAa(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false, grpId);
	}

	std::vector<float> gGABAaVec;
	for (int i=groupConfigs[0][grpId].StartN; i<=groupConfigs[0][grpId].EndN; i++) {
		gGABAaVec.push_back(managerRuntimeData.gGABAa[i]);
	}
	return gGABAaVec;
}

std::vector<float> SNN::getConductanceGABAb(int grpId) {
	assert(isSimulationWithCOBA());

	// need to copy data from GPU first
	if (getSimMode()==GPU_MODE)
		copyConductanceGABAb(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false, grpId);

	std::vector<float> gGABAbVec;
	if (isSimulationWithGABAbRise()) {
		// need to construct conductance from rise and decay parts
		for (int i=groupConfigs[0][grpId].StartN; i<=groupConfigs[0][grpId].EndN; i++) {
			gGABAbVec.push_back(managerRuntimeData.gGABAb_d[i]-managerRuntimeData.gGABAb_r[i]);
		}
	} else {
		for (int i=groupConfigs[0][grpId].StartN; i<=groupConfigs[0][grpId].EndN; i++) {
			gGABAbVec.push_back(managerRuntimeData.gGABAb[i]);
		}
	}
	return gGABAbVec;
}

// returns RangeDelay struct of a connection
RangeDelay SNN::getDelayRange(short int connId) {
	assert(connId>=0 && connId<numConnections);

	return RangeDelay(connectConfigMap[connId].minDelay, connectConfigMap[connId].maxDelay);
}


// this is a user function
// \FIXME: fix this
uint8_t* SNN::getDelays(int gIDpre, int gIDpost, int& Npre, int& Npost, uint8_t* delays) {
	Npre = groupConfigs[0][gIDpre].SizeN;
	Npost = groupConfigs[0][gIDpost].SizeN;

	if (delays == NULL) delays = new uint8_t[Npre*Npost];
	memset(delays,0,Npre*Npost);

	for (int i=groupConfigs[0][gIDpre].StartN;i<groupConfigs[0][gIDpre].EndN;i++) {
		unsigned int offset = managerRuntimeData.cumulativePost[i];

		for (int t=0;t<maxDelay_;t++) {
			delay_info_t dPar = managerRuntimeData.postDelayInfo[i*(maxDelay_+1)+t];

			for(int idx_d=dPar.delay_index_start; idx_d<(dPar.delay_index_start+dPar.delay_length); idx_d++) {
				// get synaptic info...
				SynInfo post_info = managerRuntimeData.postSynapticIds[offset + idx_d];

				// get neuron id
				//int p_i = (post_info&POST_SYN_NEURON_MASK);
				int p_i = GET_CONN_NEURON_ID(post_info);
				assert(p_i<numN);

				if (p_i >= groupConfigs[0][gIDpost].StartN && p_i <= groupConfigs[0][gIDpost].EndN) {
					// get syn id
					int s_i = GET_CONN_SYN_ID(post_info);

					// get the cumulative position for quick access...
					unsigned int pos_i = managerRuntimeData.cumulativePre[p_i] + s_i;

					delays[i+Npre*(p_i-groupConfigs[0][gIDpost].StartN)] = t+1;
				}
			}
		}
	}
	return delays;
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, where group(connect)Config[] might not be available
// or group(connect)ConfigMap is not sync with group(connect)Config[]
Grid3D SNN::getGroupGrid3D(int grpId) {
	assert(grpId>=0 && grpId<numGroups);
	return Grid3D(groupConfigMap[grpId].SizeX, groupConfigMap[grpId].SizeY, groupConfigMap[grpId].SizeZ);
}

// find ID of group with name grpName
int SNN::getGroupId(std::string grpName) {
	for (int grpId=0; grpId<numGroups; grpId++) {
		if (groupInfo[grpId].Name.compare(grpName)==0)
			return grpId;
	}

	// group not found
	return -1;
}

//GroupConfigRT SNN::getGroupConfig(int grpId) {
//	assert(grpId>=-1 && grpId<numGroups);
//	return groupConfigs[0][grpId];
//}

std::string SNN::getGroupName(int grpId) {
	assert(grpId>=-1 && grpId<numGroups);

	if (grpId==ALL)
		return "ALL";

	return groupInfo[grpId].Name;
}

GroupSTDPInfo SNN::getGroupSTDPInfo(int grpId) {
	GroupSTDPInfo gInfo;

	gInfo.WithSTDP = groupConfigs[0][grpId].WithSTDP;
	gInfo.WithESTDP = groupConfigs[0][grpId].WithESTDP;
	gInfo.WithISTDP = groupConfigs[0][grpId].WithISTDP;
	gInfo.WithESTDPtype = groupConfigs[0][grpId].WithESTDPtype;
	gInfo.WithISTDPtype = groupConfigs[0][grpId].WithISTDPtype;
	gInfo.WithESTDPcurve = groupConfigs[0][grpId].WithESTDPcurve;
	gInfo.WithISTDPcurve = groupConfigs[0][grpId].WithISTDPcurve;
	gInfo.ALPHA_MINUS_EXC = groupConfigs[0][grpId].ALPHA_MINUS_EXC;
	gInfo.ALPHA_PLUS_EXC = groupConfigs[0][grpId].ALPHA_PLUS_EXC;
	gInfo.TAU_MINUS_INV_EXC = groupConfigs[0][grpId].TAU_MINUS_INV_EXC;
	gInfo.TAU_PLUS_INV_EXC = groupConfigs[0][grpId].TAU_PLUS_INV_EXC;
	gInfo.ALPHA_MINUS_INB = groupConfigs[0][grpId].ALPHA_MINUS_INB;
	gInfo.ALPHA_PLUS_INB = groupConfigs[0][grpId].ALPHA_PLUS_INB;
	gInfo.TAU_MINUS_INV_INB = groupConfigs[0][grpId].TAU_MINUS_INV_INB;
	gInfo.TAU_PLUS_INV_INB = groupConfigs[0][grpId].TAU_PLUS_INV_INB;
	gInfo.GAMMA = groupConfigs[0][grpId].GAMMA;
	gInfo.BETA_LTP = groupConfigs[0][grpId].BETA_LTP;
	gInfo.BETA_LTD = groupConfigs[0][grpId].BETA_LTD;
	gInfo.LAMBDA = groupConfigs[0][grpId].LAMBDA;
	gInfo.DELTA = groupConfigs[0][grpId].DELTA;

	return gInfo;
}

GroupNeuromodulatorInfo SNN::getGroupNeuromodulatorInfo(int grpId) {
	GroupNeuromodulatorInfo gInfo;

	gInfo.baseDP = groupConfigs[0][grpId].baseDP;
	gInfo.base5HT = groupConfigs[0][grpId].base5HT;
	gInfo.baseACh = groupConfigs[0][grpId].baseACh;
	gInfo.baseNE = groupConfigs[0][grpId].baseNE;
	gInfo.decayDP = groupConfigs[0][grpId].decayDP;
	gInfo.decay5HT = groupConfigs[0][grpId].decay5HT;
	gInfo.decayACh = groupConfigs[0][grpId].decayACh;
	gInfo.decayNE = groupConfigs[0][grpId].decayNE;

	return gInfo;
}

// FIXME: distinguish the function call at CONFIG_STATE, SETUP_STATE, EXE_STATE, where groupConfigs[0][] might not be available
// or groupConfigMap is not sync with groupConfigs[0][]
Point3D SNN::getNeuronLocation3D(int neurId) {
	assert(neurId>=0 && neurId<numN);
	int grpId = managerRuntimeData.grpIds[neurId];
	assert(neurId>=groupConfigMap[grpId].StartN && neurId<=groupConfigMap[grpId].EndN);

	// adjust neurId for neuron ID of first neuron in the group
	neurId -= groupConfigMap[grpId].StartN;

	return getNeuronLocation3D(grpId, neurId);
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, EXE_STATE, where groupConfigs[0][] might not be available
// or groupConfigMap is not sync with groupConfigs[0][]
Point3D SNN::getNeuronLocation3D(int grpId, int relNeurId) {
	assert(grpId>=0 && grpId<numGroups);
	assert(relNeurId>=0 && relNeurId<getGroupNumNeurons(grpId));

	// coordinates are in x e[-SizeX/2,SizeX/2], y e[-SizeY/2,SizeY/2], z e[-SizeZ/2,SizeZ/2]
	// instead of x e[0,SizeX], etc.
	int intX = relNeurId % groupConfigMap[grpId].SizeX;
	int intY = (relNeurId/groupConfigMap[grpId].SizeX)%groupConfigMap[grpId].SizeY;
	int intZ = relNeurId/(groupConfigMap[grpId].SizeX*groupConfigMap[grpId].SizeY);

	// so subtract SizeX/2, etc. to get coordinates center around origin
	double coordX = 1.0*intX - (groupConfigMap[grpId].SizeX-1)/2.0;
	double coordY = 1.0*intY - (groupConfigMap[grpId].SizeY-1)/2.0;
	double coordZ = 1.0*intZ - (groupConfigMap[grpId].SizeZ-1)/2.0;
	return Point3D(coordX, coordY, coordZ);
}

// returns the number of synaptic connections associated with this connection.
int SNN::getNumSynapticConnections(short int connId) {
	//we didn't find the connection.
	if (connectConfigMap.find(connId) == connectConfigMap.end()) {
		KERNEL_ERROR("Connection ID was not found.  Quitting.");
		exitSimulation(1);
	}

	return connectConfigMap[connId].numberOfConnections;
}

// return spike buffer, which contains #spikes per neuron in the group
int* SNN::getSpikeCounter(int grpId) {
	assert(grpId>=0); assert(grpId<numGroups);

	if (!groupConfigs[0][grpId].withSpikeCounter)
		return NULL;

	// determine whether spike counts are currently stored on CPU or GPU side
	bool retrieveSpikesFromGPU = simMode_==GPU_MODE;
	if (groupConfigs[0][grpId].isSpikeGenerator) {
		// this flag should be set if group was created via CARLsim::createSpikeGeneratorGroup
		// could be SpikeGen callback or PoissonRate
		if (groupConfigs[0][grpId].RatePtr != NULL) {
			// group is Poisson group
			// even though mean rates might be on either CPU or GPU (RatePtr->isOnGPU()), in GPU mode the
			// actual random numbers will always be generated on the GPU
//			retrieveSpikesFromGPU = simMode_==GPU_MODE;
		} else {
			// group is generator with callback, CPU only
			retrieveSpikesFromGPU = false;
		}
	}

	// retrieve spikes from either CPU or GPU
	if (retrieveSpikesFromGPU) {
		return getSpikeCounter_GPU(grpId);
	} else {
		int bufPos = groupConfigs[0][grpId].spkCntBufPos; // retrieve buf pos
		return spkCntBuf[bufPos]; // return pointer to buffer
	}
}

// returns pointer to existing SpikeMonitor object, NULL else
SpikeMonitor* SNN::getSpikeMonitor(int grpId) {
	assert(grpId>=0 && grpId<getNumGroups());
	if (groupConfigs[0][grpId].SpikeMonitorId>=0) {
		return spikeMonList[(groupConfigs[0][grpId].SpikeMonitorId)];
	} else {
		return NULL;
	}
}

SpikeMonitorCore* SNN::getSpikeMonitorCore(int grpId) {
	assert(grpId>=0 && grpId<getNumGroups());
	if (groupConfigs[0][grpId].SpikeMonitorId>=0) {
		return spikeMonCoreList[(groupConfigs[0][grpId].SpikeMonitorId)];
	} else {
		return NULL;
	}
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, where group(connect)Config[] might not be available
// or group(connect)ConfigMap is not sync with group(connect)Config[]
// returns RangeWeight struct of a connection
RangeWeight SNN::getWeightRange(short int connId) {
	assert(connId>=0 && connId<numConnections);

	return RangeWeight(0.0f, connectConfigMap[connId].initWt, connectConfigMap[connId].maxWt);
}


/// **************************************************************************************************************** ///
/// PRIVATE METHODS
/// **************************************************************************************************************** ///

// all unsafe operations of SNN constructor
void SNN::SNNinit() {
	// initialize snnState
	snnState = CONFIG_SNN;
	
	// set logger mode (defines where to print all status, error, and debug messages)
	switch (loggerMode_) {
	case USER:
		fpInf_ = stdout;
		fpErr_ = stderr;
		#if defined(WIN32) || defined(WIN64)
			fpDeb_ = fopen("nul","w");
		#else
			fpDeb_ = fopen("/dev/null","w");
		#endif
		break;
	case DEVELOPER:
		fpInf_ = stdout;
		fpErr_ = stderr;
		fpDeb_ = stdout;
		break;
	case SHOWTIME:
		#if defined(WIN32) || defined(WIN64)
			fpInf_ = fopen("nul","w");
		#else
			fpInf_ = fopen("/dev/null","w");
		#endif
		fpErr_ = stderr;
		#if defined(WIN32) || defined(WIN64)
			fpDeb_ = fopen("nul","w");
		#else
			fpDeb_ = fopen("/dev/null","w");
		#endif
		break;
	case SILENT:
	case CUSTOM:
		#if defined(WIN32) || defined(WIN64)
			fpInf_ = fopen("nul","w");
			fpErr_ = fopen("nul","w");
			fpDeb_ = fopen("nul","w");
		#else
			fpInf_ = fopen("/dev/null","w");
			fpErr_ = fopen("/dev/null","w");
			fpDeb_ = fopen("/dev/null","w");
		#endif
	break;
	default:
		fpErr_ = stderr; // need to open file stream first
		KERNEL_ERROR("Unknown logger mode");
		exit(1);
	}
	#if defined(WIN32) || defined(WIN64)
		fpLog_= fopen("results\\carlsim.log","w");
	#else
		fpLog_ = fopen("results/carlsim.log","w");
	#endif

	#ifdef __REGRESSION_TESTING__
	#if defined(WIN32) || defined(WIN64)
		fpInf_ = fopen("nul","w");
		fpErr_ = fopen("nul","w");
		fpDeb_ = fopen("nul","w");
	#else
		fpInf_ = fopen("/dev/null","w");
		fpErr_ = fopen("/dev/null","w");
		fpDeb_ = fopen("/dev/null","w");
	#endif
	#endif

	KERNEL_INFO("*********************************************************************************");
	KERNEL_INFO("********************      Welcome to CARLsim %d.%d      ***************************",
				MAJOR_VERSION,MINOR_VERSION);
	KERNEL_INFO("*********************************************************************************\n");

	KERNEL_INFO("***************************** Configuring Network ********************************");
	KERNEL_INFO("Starting CARLsim simulation \"%s\" in %s mode",networkName_.c_str(),
		loggerMode_string[loggerMode_]);
	KERNEL_INFO("Random number seed: %d",randSeed_);

	time_t rawtime;
	struct tm * timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	KERNEL_DEBUG("Current local time and date: %s", asctime(timeinfo));

	// init random seed
	srand48(randSeed_);

	simTimeRunStart = 0; simTimeRunStop = 0;
	simTimeLastRunSummary = 0;
	simTimeMs = 0; simTimeSec = 0; simTime = 0;
	spikeCountSec = 0; spikeCountD1Sec = 0; spikeCountD2Sec = 0;
	spikeCount = 0; spikeCountD2 = 0; spikeCountD1 = 0;
	nPoissonSpikes = 0;

	numGroups = 0;
	numConnections = 0;
	numSpikeGenGrps = 0;
	NgenFunc = 0;
	simulatorDeleted = false;

	cumExecutionTime = 0.0;
	cpuExecutionTime = 0.0;
	gpuExecutionTime = 0.0;

	spikeRateUpdated = false;
	numSpikeMonitor = 0;
	numGroupMonitor = 0;
	numConnectionMonitor = 0;
	numSpkCnt = 0;

	sim_with_fixedwts = true; // default is true, will be set to false if there are any plastic synapses
	sim_with_conductances = false; // default is false
	sim_with_stdp = false;
	sim_with_modulated_stdp = false;
	sim_with_homeostasis = false;
	sim_with_stp = false;
	sim_in_testing = false;

	//maxSpikesD1 = 0;
	//maxSpikesD2 = 0;

	loadSimFID = NULL;

	numN = 0;
	numNPois = 0;
	numNExcPois = 0;
	numNInhPois = 0;
	numNReg = 0;
	numNExcReg = 0;
	numNInhReg = 0;

	maxDelay_ = 0;

	// conductance info struct for simulation
	sim_with_NMDA_rise = false;
	sim_with_GABAb_rise = false;
	dAMPA  = 1.0-1.0/5.0;		// some default decay and rise times
	rNMDA  = 1.0-1.0/10.0;
	dNMDA  = 1.0-1.0/150.0;
	sNMDA  = 1.0;
	dGABAa = 1.0-1.0/6.0;
	rGABAb = 1.0-1.0/100.0;
	dGABAb = 1.0-1.0/150.0;
	sGABAb = 1.0;

	// each SNN object hold its own random number object
	gpuPoissonRand = NULL;

	// reset all monitors, don't deallocate (false)
	resetMonitors(false);

	resetGroupConfigs(false);

	resetConnectionConfigs(false);

	// reset all runtime data, don't deallocate (false)
	resetRuntimeData(false);

	memset(&cpuSnnSz, 0, sizeof(cpuSnnSz));

	// initialize propogated spike buffers.....
	pbuf = new PropagatedSpikeBuffer(0, PROPAGATED_BUFFER_SIZE);

	memset(networkConfigs, 0, sizeof(NetworkConfigRT) * MAX_NET_PER_SNN);
	
	memset(gpuRuntimeData, 0, sizeof(RuntimeData) * MAX_NET_PER_SNN);
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) // FIXME: redundant??
		gpuRuntimeData[netId].allocated = false;

	memset(&managerRuntimeData, 0, sizeof(RuntimeData));
	managerRuntimeData.allocated = false; // FIXME: redundant??

	//for (int i=0; i < MAX_GRP_PER_SNN; i++) {
	//	groupConfigs[0][i].Type = UNKNOWN_NEURON;
	//	groupConfigs[0][i].MaxFiringRate = UNKNOWN_NEURON_MAX_FIRING_RATE;
	//	groupConfigs[0][i].SpikeMonitorId = -1;
	//	groupConfigs[0][i].GroupMonitorId = -1;
	//	groupConfigs[0][i].numPostSynapses = 0;	// default value
	//	groupConfigs[0][i].numPreSynapses = 0;	// default value
	//	groupConfigs[0][i].WithSTP = false;
	//	groupConfigs[0][i].WithSTDP = false;
	//	groupConfigs[0][i].WithESTDP = false;
	//	groupConfigs[0][i].WithISTDP = false;
	//	groupConfigs[0][i].WithESTDPtype = UNKNOWN_STDP;
	//	groupConfigs[0][i].WithISTDPtype = UNKNOWN_STDP;
	//	groupConfigs[0][i].WithESTDPcurve = UNKNOWN_CURVE;
	//	groupConfigs[0][i].WithISTDPcurve = UNKNOWN_CURVE;
	//	groupConfigs[0][i].FixedInputWts = true; // Default is true. This value changed to false
	//	// if any incoming  connections are plastic
	//	groupConfigs[0][i].isSpikeGenerator = false;
	//	groupConfigs[0][i].RatePtr = NULL;

	//	groupConfigs[0][i].homeoId = -1;
	//	groupConfigs[0][i].avgTimeScale  = 10000.0;

	//	groupConfigs[0][i].baseDP = 1.0f;
	//	groupConfigs[0][i].base5HT = 1.0f;
	//	groupConfigs[0][i].baseACh = 1.0f;
	//	groupConfigs[0][i].baseNE = 1.0f;
	//	groupConfigs[0][i].decayDP = 1 - (1.0f / 100);
	//	groupConfigs[0][i].decay5HT = 1 - (1.0f / 100);
	//	groupConfigs[0][i].decayACh = 1 - (1.0f / 100);
	//	groupConfigs[0][i].decayNE = 1 - (1.0f / 100);

	//	groupConfigs[0][i].spikeGen = NULL;

	//	groupConfigs[0][i].withSpikeCounter = false;
	//	groupConfigs[0][i].spkCntRecordDur = -1;
	//	groupConfigs[0][i].spkCntRecordDurHelper = 0;
	//	groupConfigs[0][i].spkCntBufPos = -1;

	//	groupConfigs[0][i].StartN       = -1;
	//	groupConfigs[0][i].EndN       	 = -1;

	//	groupConfigs[0][i].CurrTimeSlice = 0;
	//	groupConfigs[0][i].NewTimeSlice = 0;
	//	groupConfigs[0][i].SliceUpdateTime = 0;
	//}

	for (int i = 0; i < MAX_GRP_PER_SNN; i++) {
		groupInfo[i].numPostConn = 0;
		groupInfo[i].numPreConn  = 0;
		groupInfo[i].maxPostConn = 0;
		groupInfo[i].maxPreConn  = 0;
		groupInfo[i].sumPostConn = 0;
		groupInfo[i].sumPreConn  = 0;

	}

	CUDA_CREATE_TIMER(timer);
	CUDA_RESET_TIMER(timer);

	// default weight update parameter
	wtANDwtChangeUpdateInterval_ = 1000; // update weights every 1000 ms (default)
	wtANDwtChangeUpdateIntervalCnt_ = 0; // helper var to implement fast modulo
	stdpScaleFactor_ = 1.0f;
	wtChangeDecay_ = 0.0f;

	if (simMode_ == GPU_MODE)
		configGPUDevice();
}

void SNN::allocateSNN() {
	// Confirm allocation of SNN runtime data in main memory
	managerRuntimeData.allocated = true;
	managerRuntimeData.memType = CPU_MODE;

	switch (simMode_) {
	case GPU_MODE:
		allocateSNN_GPU();
		break;
	case CPU_MODE:
		allocateSNN_CPU();
		break;
	default:
		KERNEL_ERROR("Unknown simMode_");
		break;
	}
}

void SNN::allocateSNN_CPU() {
	managerRuntimeData.allocated = true;
	managerRuntimeData.memType = CPU_MODE;
}

void SNN::allocateRuntimeData() {
	managerRuntimeData.voltage    = new float[managerRTDSize.maxNumNReg];
	managerRuntimeData.recovery   = new float[managerRTDSize.maxNumNReg];
	managerRuntimeData.Izh_a      = new float[managerRTDSize.maxNumNReg];
	managerRuntimeData.Izh_b      = new float[managerRTDSize.maxNumNReg];
	managerRuntimeData.Izh_c      = new float[managerRTDSize.maxNumNReg];
	managerRuntimeData.Izh_d      = new float[managerRTDSize.maxNumNReg];
	managerRuntimeData.current    = new float[managerRTDSize.maxNumNReg];
	managerRuntimeData.extCurrent = new float[managerRTDSize.maxNumNReg];
	memset(managerRuntimeData.voltage, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	memset(managerRuntimeData.recovery, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	memset(managerRuntimeData.Izh_a, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	memset(managerRuntimeData.Izh_b, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	memset(managerRuntimeData.Izh_c, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	memset(managerRuntimeData.Izh_d, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	memset(managerRuntimeData.current, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	memset(managerRuntimeData.extCurrent, 0, sizeof(float) * managerRTDSize.maxNumNReg);
	cpuSnnSz.neuronInfoSize += (sizeof(float) * managerRTDSize.maxNumNReg * 8);

	if (sim_with_conductances) {
		managerRuntimeData.gAMPA  = new float[managerRTDSize.maxNumNReg];
		managerRuntimeData.gGABAa = new float[managerRTDSize.maxNumNReg];
		memset(managerRuntimeData.gAMPA, 0, sizeof(float) * managerRTDSize.maxNumNReg);
		memset(managerRuntimeData.gGABAa, 0, sizeof(float) * managerRTDSize.maxNumNReg);
		cpuSnnSz.neuronInfoSize += sizeof(float) * managerRTDSize.maxNumNReg * 2;

		if (sim_with_NMDA_rise) {
			// If NMDA rise time is enabled, we'll have to compute NMDA conductance in two steps (using an exponential
			// for the rise time and one for the decay time)
			managerRuntimeData.gNMDA_r = new float[managerRTDSize.maxNumNReg];
			managerRuntimeData.gNMDA_d = new float[managerRTDSize.maxNumNReg];
			memset(managerRuntimeData.gNMDA_r, 0, sizeof(float) * managerRTDSize.maxNumNReg);
			memset(managerRuntimeData.gNMDA_d, 0, sizeof(float) * managerRTDSize.maxNumNReg);
			cpuSnnSz.neuronInfoSize += sizeof(float) * managerRTDSize.maxNumNReg * 2;
		} else {
			managerRuntimeData.gNMDA = new float[managerRTDSize.maxNumNReg];
			memset(managerRuntimeData.gNMDA, 0, sizeof(float) * managerRTDSize.maxNumNReg);
			cpuSnnSz.neuronInfoSize += sizeof(float) * managerRTDSize.maxNumNReg;
		}

		if (sim_with_GABAb_rise) {
			managerRuntimeData.gGABAb_r = new float[managerRTDSize.maxNumNReg];
			managerRuntimeData.gGABAb_d = new float[managerRTDSize.maxNumNReg];
			memset(managerRuntimeData.gGABAb_r, 0, sizeof(float) * managerRTDSize.maxNumNReg);
			memset(managerRuntimeData.gGABAb_d, 0, sizeof(float) * managerRTDSize.maxNumNReg);
			cpuSnnSz.neuronInfoSize += sizeof(float) * managerRTDSize.maxNumNReg * 2;
		} else {
			managerRuntimeData.gGABAb = new float[managerRTDSize.maxNumNReg];
			memset(managerRuntimeData.gGABAb, 0, sizeof(float) * managerRTDSize.maxNumNReg);
			cpuSnnSz.neuronInfoSize += sizeof(float)*managerRTDSize.maxNumNReg;
		}
	}


	managerRuntimeData.grpDA  = new float[managerRTDSize.maxNumGroups];
	managerRuntimeData.grp5HT = new float[managerRTDSize.maxNumGroups];
	managerRuntimeData.grpACh = new float[managerRTDSize.maxNumGroups];
	managerRuntimeData.grpNE  = new float[managerRTDSize.maxNumGroups];
	memset(managerRuntimeData.grpDA, 0, sizeof(float) * managerRTDSize.maxNumGroups);
	memset(managerRuntimeData.grp5HT, 0, sizeof(float) * managerRTDSize.maxNumGroups);
	memset(managerRuntimeData.grpACh, 0, sizeof(float) * managerRTDSize.maxNumGroups);
	memset(managerRuntimeData.grpNE, 0, sizeof(float) * managerRTDSize.maxNumGroups);

	// init neuromodulators and their assistive buffers
	for (int i = 0; i < managerRTDSize.maxNumGroups; i++) {
		managerRuntimeData.grpDABuffer[i]  = new float[1000]; // 1 second DA buffer
		managerRuntimeData.grp5HTBuffer[i] = new float[1000];
		managerRuntimeData.grpAChBuffer[i] = new float[1000];
		managerRuntimeData.grpNEBuffer[i]  = new float[1000];
		memset(managerRuntimeData.grpDABuffer[i], 0, sizeof(float) * 1000);
		memset(managerRuntimeData.grp5HTBuffer[i], 0, sizeof(float) * 1000);
		memset(managerRuntimeData.grpAChBuffer[i], 0, sizeof(float) * 1000);
		memset(managerRuntimeData.grpNEBuffer[i], 0, sizeof(float) * 1000);
	}

	managerRuntimeData.lastSpikeTime	= new int[managerRTDSize.maxNumN];
	memset(managerRuntimeData.lastSpikeTime, 0, sizeof(int) * managerRTDSize.maxNumN);
	cpuSnnSz.neuronInfoSize += sizeof(int) * managerRTDSize.maxNumN;
	
	managerRuntimeData.nSpikeCnt  = new int[managerRTDSize.maxNumN];
	memset(managerRuntimeData.nSpikeCnt, 0, sizeof(int) * managerRTDSize.maxNumN);

	//! homeostasis variables
	if (sim_with_homeostasis) {
		managerRuntimeData.avgFiring  = new float[managerRTDSize.maxNumN];
		managerRuntimeData.baseFiring = new float[managerRTDSize.maxNumN];
		memset(managerRuntimeData.avgFiring, 0, sizeof(float) * managerRTDSize.maxNumN);
		memset(managerRuntimeData.baseFiring, 0, sizeof(float) * managerRTDSize.maxNumN);
	}

	// STP can be applied to spike generators, too -> numN
	if (sim_with_stp) {
		// \TODO: The size of these data structures could be reduced to the max synaptic delay of all
		// connections with STP. That number might not be the same as maxDelay_.
		managerRuntimeData.stpu = new float[managerRTDSize.maxNumN * (maxDelay_ + 1)];
		managerRuntimeData.stpx = new float[managerRTDSize.maxNumN*(maxDelay_ + 1)];
		memset(managerRuntimeData.stpu, 0, sizeof(float) * managerRTDSize.maxNumN * (maxDelay_ + 1)); // memset works for 0.0
		memset(managerRuntimeData.stpx, 0, sizeof(float) * managerRTDSize.maxNumN * (maxDelay_ + 1));
		cpuSnnSz.synapticInfoSize += (2 * sizeof(float) * managerRTDSize.maxNumN * (maxDelay_ + 1));
	}

	managerRuntimeData.Npre           = new unsigned short[managerRTDSize.maxNumN];
	managerRuntimeData.Npre_plastic   = new unsigned short[managerRTDSize.maxNumN];
	managerRuntimeData.Npost          = new unsigned short[managerRTDSize.maxNumN];
	managerRuntimeData.cumulativePost = new unsigned int[managerRTDSize.maxNumN];
	managerRuntimeData.cumulativePre  = new unsigned int[managerRTDSize.maxNumN];
	memset(managerRuntimeData.Npre, 0, sizeof(short) * managerRTDSize.maxNumN);
	memset(managerRuntimeData.Npre_plastic, 0, sizeof(short) * managerRTDSize.maxNumN);
	memset(managerRuntimeData.Npost, 0, sizeof(short) * managerRTDSize.maxNumN);
	memset(managerRuntimeData.cumulativePost, 0, sizeof(int) * managerRTDSize.maxNumN);
	memset(managerRuntimeData.cumulativePre, 0, sizeof(int) * managerRTDSize.maxNumN);
	cpuSnnSz.networkInfoSize += (int)(sizeof(int) * managerRTDSize.maxNumN * 3.5);

	managerRuntimeData.postSynapticIds = new SynInfo[managerRTDSize.maxNumPostSynNet];
	managerRuntimeData.postDelayInfo   = new delay_info_t[managerRTDSize.maxNumN * (maxDelay_ + 1)];	//!< Possible delay values are 0....maxDelay_ (inclusive of maxDelay_)
	memset(managerRuntimeData.postSynapticIds, 0, sizeof(SynInfo) * managerRTDSize.maxNumPostSynNet);
	memset(managerRuntimeData.postDelayInfo, 0, sizeof(delay_info_t) * managerRTDSize.maxNumN * (maxDelay_ + 1));
	cpuSnnSz.networkInfoSize += (sizeof(SynInfo) * managerRTDSize.maxNumPostSynNet) + (sizeof(delay_info_t) * managerRTDSize.maxNumN * (maxDelay_ + 1));

	managerRuntimeData.wt           = new float[managerRTDSize.maxNumPreSynNet];
	managerRuntimeData.wtChange     = new float[managerRTDSize.maxNumPreSynNet];
	managerRuntimeData.maxSynWt     = new float[managerRTDSize.maxNumPreSynNet];
	managerRuntimeData.synSpikeTime = new int[managerRTDSize.maxNumPreSynNet];
	memset(managerRuntimeData.wt, 0, sizeof(float) * managerRTDSize.maxNumPreSynNet);
	memset(managerRuntimeData.wtChange, 0, sizeof(float) * managerRTDSize.maxNumPreSynNet);
	memset(managerRuntimeData.maxSynWt, 0, sizeof(float) * managerRTDSize.maxNumPreSynNet);
	memset(managerRuntimeData.synSpikeTime, 0, sizeof(int) * managerRTDSize.maxNumPreSynNet);
	cpuSnnSz.synapticInfoSize += sizeof(float) * managerRTDSize.maxNumPreSynNet * 4;

	mulSynFast = new float[managerRTDSize.maxNumConnections];
	mulSynSlow = new float[managerRTDSize.maxNumConnections];
	memset(mulSynFast, 0, sizeof(float) * managerRTDSize.maxNumConnections);
	memset(mulSynSlow, 0, sizeof(float) * managerRTDSize.maxNumConnections);
	cpuSnnSz.synapticInfoSize += sizeof(float) * managerRTDSize.maxNumConnections * 2;


	managerRuntimeData.connIdsPreIdx	= new short int[managerRTDSize.maxNumPreSynNet];
	memset(managerRuntimeData.connIdsPreIdx, 0, sizeof(short int) * managerRTDSize.maxNumPreSynNet);
	cpuSnnSz.synapticInfoSize += sizeof(short int) * managerRTDSize.maxNumPreSynNet;

	managerRuntimeData.preSynapticIds	= new SynInfo[managerRTDSize.maxNumPreSynNet];
	memset(managerRuntimeData.preSynapticIds, 0, sizeof(SynInfo) * managerRTDSize.maxNumPreSynNet);
	cpuSnnSz.networkInfoSize += sizeof(SynInfo) * managerRTDSize.maxNumPreSynNet;

	managerRuntimeData.grpIds = new short int[managerRTDSize.maxNumN];
	memset(managerRuntimeData.grpIds, 0, sizeof(short int) * managerRTDSize.maxNumN);
	cpuSnnSz.neuronInfoSize += sizeof(short int) * managerRTDSize.maxNumN;

	// poisson Firing Rate
	// cpuSnnSz.neuronInfoSize += (sizeof(int) * networkConfigs[0].numNPois);
}


int SNN::addSpikeToTable(int nid, int g) {
	int spikeBufferFull = 0;
	managerRuntimeData.lastSpikeTime[nid] = simTime;
	managerRuntimeData.nSpikeCnt[nid]++;
	if (sim_with_homeostasis)
		managerRuntimeData.avgFiring[nid] += 1000/(groupConfigs[0][g].avgTimeScale*1000);

	if (simMode_ == GPU_MODE) {
		assert(groupConfigs[0][g].isSpikeGenerator == true);
		setSpikeGenBit_GPU(nid, g);
		return 0;
	}

	// update STP for poinsson (input) neurons
	if (groupConfigs[0][g].WithSTP) {
		// update the spike-dependent part of du/dt and dx/dt
		// we need to retrieve the STP values from the right buffer position (right before vs. right after the spike)
		int ind_plus = STP_BUF_POS(nid,simTime); // index of right after the spike, such as in u^+
	    int ind_minus = STP_BUF_POS(nid,(simTime-1)); // index of right before the spike, such as in u^-

		// du/dt = -u/tau_F + U * (1-u^-) * \delta(t-t_{spk})
		managerRuntimeData.stpu[ind_plus] += groupConfigs[0][g].STP_U*(1.0-managerRuntimeData.stpu[ind_minus]);

		// dx/dt = (1-x)/tau_D - u^+ * x^- * \delta(t-t_{spk})
		managerRuntimeData.stpx[ind_plus] -= managerRuntimeData.stpu[ind_plus]*managerRuntimeData.stpx[ind_minus];
	}

	// insert poisson (input) spikes into firingTableD1(D2)
	if (groupConfigs[0][g].MaxDelay == 1) {
		assert(nid < numN);
		managerRuntimeData.firingTableD1[spikeCountD1Sec] = nid;
		spikeCountD1Sec++;
		if (spikeCountD1Sec >= networkConfigs[0].maxSpikesD1) {
			spikeBufferFull = 2;
			spikeCountD1Sec = networkConfigs[0].maxSpikesD1-1;
		}
	} else {
		assert(nid < numN);
		managerRuntimeData.firingTableD2[spikeCountD2Sec] = nid;
		spikeCountD2Sec++;
		if (spikeCountD2Sec >= networkConfigs[0].maxSpikesD2) {
			spikeBufferFull = 1;
			spikeCountD2Sec = networkConfigs[0].maxSpikesD2-1;
		}
	}
	return spikeBufferFull;
}

int SNN::assignGroup(int grpId, int availableNeuronId) {
	int newAvailableNeuronId;
	assert(groupConfigMap[grpId].StartN == -1); // The group has not yet been assigned
	groupConfigMap[grpId].StartN = availableNeuronId;
	groupConfigMap[grpId].EndN = availableNeuronId + groupConfigMap[grpId].SizeN - 1;

	KERNEL_DEBUG("Allocation for %d(%s), St=%d, End=%d",
				grpId, groupInfo[grpId].Name.c_str(), groupConfigMap[grpId].StartN, groupConfigMap[grpId].EndN);

	newAvailableNeuronId = availableNeuronId + groupConfigMap[grpId].SizeN;
	assert(newAvailableNeuronId <= numN);

	return newAvailableNeuronId;
}

int SNN::assignGroup(std::list<GroupConfigRT>::iterator grpIt, int localGroupId, int availableNeuronId) {
	int newAvailableNeuronId;
	assert(grpIt->localGrpId == -1); // The group has not yet been assigned
	grpIt->localGrpId = localGroupId;
	grpIt->localStartN = availableNeuronId;
	grpIt->localEndN = availableNeuronId + grpIt->SizeN - 1;

	grpIt->LtoGOffset = grpIt->StartN - grpIt->localStartN;
	grpIt->GtoLOffset = grpIt->localStartN - grpIt->StartN;

	KERNEL_DEBUG("Allocation for group (%s) [id:%d, local id:%d], St=%d, End=%d", groupInfo[grpIt->grpId].Name.c_str(),
		grpIt->grpId, grpIt->localGrpId, grpIt->localStartN, grpIt->localEndN);

	newAvailableNeuronId = availableNeuronId + grpIt->SizeN;

	return newAvailableNeuronId;
}

void SNN::generateGroupRuntime(int grpId) {
	resetNeuromodulator(grpId);

	for(int i = groupConfigs[0][grpId].StartN; i <= groupConfigs[0][grpId].EndN; i++)
		resetNeuron(i, grpId);
}

void SNN::generateGroupConfigs() {
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		for (std::list<GroupConfigRT>::iterator grpIt = groupPartitionLists[netId].begin(); grpIt != groupPartitionLists[netId].end(); grpIt++) {
			groupConfigs[netId][grpIt->localGrpId] = *grpIt;
		}
	}
}

void SNN::generateConnectConfigs() {
	// for future use
}

void SNN::generateNetworkConfigs() {
	// reset manager runtime data size
	memset(&managerRTDSize, 0, sizeof(ManagerRuntimeDataSize));

	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		if (!groupPartitionLists[netId].empty()) {
			// copy the global network config to local network configs
			// global configuration for maximum axonal delay
			networkConfigs[netId].maxDelay  = maxDelay_;
	
			// configurations for execution features
			networkConfigs[netId].sim_with_fixedwts = sim_with_fixedwts;
			networkConfigs[netId].sim_with_conductances = sim_with_conductances;
			networkConfigs[netId].sim_with_homeostasis = sim_with_homeostasis;
			networkConfigs[netId].sim_with_stdp = sim_with_stdp;
			networkConfigs[netId].sim_with_stp = sim_with_stp;
			networkConfigs[netId].sim_in_testing = sim_in_testing;

			// stdp, da-stdp configurations
			networkConfigs[netId].stdpScaleFactor = stdpScaleFactor_;
			networkConfigs[netId].wtChangeDecay = wtChangeDecay_;

			// conductance configurations
			networkConfigs[netId].sim_with_NMDA_rise = sim_with_NMDA_rise;
			networkConfigs[netId].sim_with_GABAb_rise = sim_with_GABAb_rise;
			networkConfigs[netId].dAMPA = dAMPA;
			networkConfigs[netId].rNMDA = rNMDA;
			networkConfigs[netId].dNMDA = dNMDA;
			networkConfigs[netId].sNMDA = sNMDA;
			networkConfigs[netId].dGABAa = dGABAa;
			networkConfigs[netId].rGABAb = rGABAb;
			networkConfigs[netId].dGABAb = dGABAb;
			networkConfigs[netId].sGABAb = sGABAb;

			// configurations for boundries of neural types
			findNumN(netId, networkConfigs[netId].numN, networkConfigs[netId].numNExternal, networkConfigs[netId].numNAssigned,
					 networkConfigs[netId].numNReg, networkConfigs[netId].numNExcReg, networkConfigs[netId].numNInhReg,
					 networkConfigs[netId].numNPois, networkConfigs[netId].numNExcPois, networkConfigs[netId].numNInhPois);
			// find the maximum number of numN and numNReg among local networks
			if (networkConfigs[netId].numN > managerRTDSize.maxNumN) managerRTDSize.maxNumN = networkConfigs[netId].numN;
			if (networkConfigs[netId].numNReg > managerRTDSize.maxNumNReg) managerRTDSize.maxNumNReg = networkConfigs[netId].numNExcReg;

			// configurations for assigned groups and connections
			networkConfigs[netId].numGroups = groupPartitionLists[netId].size();
			networkConfigs[netId].numConnections = localConnectLists[netId].size() + externalConnectLists[netId].size();
			// find the maximum number of numGroups and numConnections among local networks
			if (networkConfigs[netId].numGroups > managerRTDSize.maxNumGroups) managerRTDSize.maxNumGroups = networkConfigs[netId].numGroups;
			if (networkConfigs[netId].numConnections > managerRTDSize.maxNumConnections) managerRTDSize.maxNumConnections = networkConfigs[netId].numConnections;
		
			// find the maximum number of pre- and post-connections among neurons
			// SNN::maxNumPreSynN and SNN::maxNumPostSynN are updated
			findMaxNumSynapsesNeurons(netId, networkConfigs[netId].maxNumPostSynN, networkConfigs[netId].maxNumPreSynN);

			// find the maximum number of spikes in D1 (i.e., maxDelay == 1) and D2 (i.e., maxDelay >= 2) sets
			findMaxSpikesD1D2(netId, networkConfigs[netId].maxSpikesD1, networkConfigs[netId].maxSpikesD2);
			// find the maximum number of maxSipkesD1(D2) among networks
			if (networkConfigs[netId].maxSpikesD1 > managerRTDSize.maxMaxSpikeD1) managerRTDSize.maxMaxSpikeD1 = networkConfigs[netId].maxSpikesD1;
			if (networkConfigs[netId].maxSpikesD2 > managerRTDSize.maxMaxSpikeD2) managerRTDSize.maxMaxSpikeD2 = networkConfigs[netId].maxSpikesD2;


			// find the total number of synapses in the network
			findNumSynapsesNetwork(netId, networkConfigs[netId].numPostSynNet, networkConfigs[netId].numPreSynNet);
			// find the maximum number of total # of pre- and post-connections among local networks
			if (networkConfigs[netId].numPreSynNet > managerRTDSize.maxNumPreSynNet) managerRTDSize.maxNumPreSynNet = networkConfigs[netId].numPreSynNet;
			if (networkConfigs[netId].numPostSynNet > managerRTDSize.maxNumPostSynNet) managerRTDSize.maxNumPostSynNet = networkConfigs[netId].numPostSynNet;
		}
	}
}

bool compareSrcNeuron(const ConnectionInfo& first, const ConnectionInfo& second) {
	return (first.nSrc < second.nSrc);
}

bool compareDelay(const ConnectionInfo& first, const ConnectionInfo& second) {
	return (first.delay < second.delay);
}

void SNN::generateConnectionRuntime() {
	// generate mulSynFast, mulSynSlow in connection-centric array
	for (std::map<int, ConnectConfig>::iterator it = connectConfigMap.begin(); it != connectConfigMap.end(); it++) {
		// store scaling factors for synaptic currents in connection-centric array
		mulSynFast[it->second.connId] = it->second.mulSynFast;
		mulSynSlow[it->second.connId] = it->second.mulSynSlow;
	}

	// parse ConnectionInfo stored in connectionLists[0]
	// generate Npost, Npre, Npre_plastic
	int parsedConnections = 0;	
	for (std::list<ConnectionInfo>::iterator it = connectionLists[0].begin(); it != connectionLists[0].end(); it++) {
		managerRuntimeData.Npost[it->nSrc]++;
		managerRuntimeData.Npre[it->nDest]++;

		if (GET_FIXED_PLASTIC(connectConfigMap[it->connId].connProp) == SYN_PLASTIC) {
			sim_with_fixedwts = false; // if network has any plastic synapses at all, this will be set to true
			managerRuntimeData.Npre_plastic[it->nDest]++;

			// homeostasis
			if (groupConfigs[0][it->grpDest].WithHomeostasis && groupConfigs[0][it->grpDest].homeoId == -1)
				groupConfigs[0][it->grpDest].homeoId = it->nDest; // this neuron info will be printed
		}

		// generate the delay vaule
		it->delay = connectConfigMap[it->connId].minDelay + rand() % (connectConfigMap[it->connId].maxDelay - connectConfigMap[it->connId].minDelay + 1);
		assert((it->delay >= connectConfigMap[it->connId].minDelay) && (it->delay <= connectConfigMap[it->connId].maxDelay));
		// generate the max weight and initial weight
		float initWt = generateWeight(connectConfigMap[it->connId].connProp, connectConfigMap[it->connId].initWt, connectConfigMap[it->connId].maxWt, it->nSrc, it->grpSrc);
		float maxWt = connectConfigMap[it->connId].maxWt;
		// adjust sign of weight based on pre-group (negative if pre is inhibitory)
		it->maxWt = isExcitatoryGroup(it->grpSrc) ? fabs(maxWt) : -1.0 * fabs(maxWt);
		it->initWt = isExcitatoryGroup(it->grpSrc) ? fabs(initWt) : -1.0 * fabs(initWt);

		parsedConnections++;
	}
	assert(parsedConnections == networkConfigs[0].numPostSynNet && parsedConnections == networkConfigs[0].numPreSynNet);

	// generate cumulativePost and cumulativePre
	managerRuntimeData.cumulativePost[0] = 0;
	managerRuntimeData.cumulativePre[0] = 0;
	for (int nId = 1; nId < numN; nId++) {
		managerRuntimeData.cumulativePost[nId] = managerRuntimeData.cumulativePost[nId - 1] + managerRuntimeData.Npost[nId - 1];
		managerRuntimeData.cumulativePre[nId] = managerRuntimeData.cumulativePre[nId - 1] + managerRuntimeData.Npre[nId - 1];
	}

	// generate preSynapticIds, parse plastic connections first
	memset(managerRuntimeData.Npre, 0, sizeof(short) * numN); // reset managerRuntimeData.Npre to zero, so that it can be used as synId
	parsedConnections = 0;
	for (std::list<ConnectionInfo>::iterator it = connectionLists[0].begin(); it != connectionLists[0].end(); it++) {
		if (GET_FIXED_PLASTIC(connectConfigMap[it->connId].connProp) == SYN_PLASTIC) {
			int pre_pos  = managerRuntimeData.cumulativePre[it->nDest] + managerRuntimeData.Npre[it->nDest];
			assert(pre_pos  < networkConfigs[0].numPreSynNet);

			managerRuntimeData.preSynapticIds[pre_pos] = SET_CONN_ID(it->nSrc, 0, it->grpSrc); // managerRuntimeData.Npost[it->nSrc] is not availabe at this parse
			it->preSynId = managerRuntimeData.Npre[it->nDest]; // save managerRuntimeData.Npre[it->nDest] as synId

			managerRuntimeData.Npre[it->nDest]++;
			parsedConnections++;

			// update the maximum number of and pre-connections of a neuron in a group
			if (managerRuntimeData.Npre[it->nDest] > groupInfo[it->grpDest].maxPreConn)
				groupInfo[it->grpDest].maxPreConn = managerRuntimeData.Npre[it->nDest];
		}
	}
	// parse fixed connections
	for (std::list<ConnectionInfo>::iterator it = connectionLists[0].begin(); it != connectionLists[0].end(); it++) {
		if (GET_FIXED_PLASTIC(connectConfigMap[it->connId].connProp) == SYN_FIXED) {
			int pre_pos  = managerRuntimeData.cumulativePre[it->nDest] + managerRuntimeData.Npre[it->nDest];
			assert(pre_pos < networkConfigs[0].numPreSynNet);

			managerRuntimeData.preSynapticIds[pre_pos] = SET_CONN_ID(it->nSrc, 0, it->grpSrc); // managerRuntimeData.Npost[it->nSrc] is not availabe at this parse
			it->preSynId = managerRuntimeData.Npre[it->nDest]; // save managerRuntimeData.Npre[it->nDest] as synId

			managerRuntimeData.Npre[it->nDest]++;
			parsedConnections++;

			// update the maximum number of and pre-connections of a neuron in a group
			if (managerRuntimeData.Npre[it->nDest] > groupInfo[it->grpDest].maxPreConn)
				groupInfo[it->grpDest].maxPreConn = managerRuntimeData.Npre[it->nDest];
		}
	}
	assert(parsedConnections == networkConfigs[0].numPreSynNet);

	// generate postSynapticIds
	connectionLists[0].sort(compareSrcNeuron);
	for (int nId = 0; nId < numN; nId++) { // pre-neuron order
		if (managerRuntimeData.Npost[nId] > 0) {
			std::list<ConnectionInfo> postConnectionList;
			ConnectionInfo targetConn;
			targetConn.nSrc = nId; // the other fields does not matter
			
			std::list<ConnectionInfo>::iterator firstPostConn = std::find(connectionLists[0].begin(), connectionLists[0].end(), targetConn);
			std::list<ConnectionInfo>::iterator lastPostConn = firstPostConn;
			std::advance(lastPostConn, managerRuntimeData.Npost[nId]);
			managerRuntimeData.Npost[nId] = 0; // reset managerRuntimeData.Npost[nId] to zero, so that it can be used as synId

			postConnectionList.splice(postConnectionList.begin(), connectionLists[0], firstPostConn, lastPostConn);
			postConnectionList.sort(compareDelay);

			int post_pos, pre_pos, lastDelay = 0;
			parsedConnections = 0;
			for (std::list<ConnectionInfo>::iterator it = postConnectionList.begin(); it != postConnectionList.end(); it++) {
				assert(it->nSrc == nId);
				post_pos = managerRuntimeData.cumulativePost[it->nSrc] + managerRuntimeData.Npost[it->nSrc];
				pre_pos  = managerRuntimeData.cumulativePre[it->nDest] + it->preSynId;

				assert(post_pos < networkConfigs[0].numPostSynNet);
				//assert(pre_pos  < numPreSynNet);

				// generate a post synaptic id for the current connection
				managerRuntimeData.postSynapticIds[post_pos] = SET_CONN_ID(it->nDest, it->preSynId, it->grpDest);// used stored managerRuntimeData.Npre[it->nDest] in it->preSynId
				// generate a delay look up table by the way
				assert(it->delay > 0);
				if (it->delay > lastDelay) {
					managerRuntimeData.postDelayInfo[nId * (maxDelay_ + 1) + it->delay - 1].delay_index_start = managerRuntimeData.Npost[it->nSrc];
					managerRuntimeData.postDelayInfo[nId * (maxDelay_ + 1) + it->delay - 1].delay_length++;
				} else if (it->delay == lastDelay) {
					managerRuntimeData.postDelayInfo[nId * (maxDelay_ + 1) + it->delay - 1].delay_length++;
				} else {
					KERNEL_ERROR("Post-synaptic delays not sorted correctly... pre_id=%d, delay[%d]=%d, delay[%d]=%d",
						nId, managerRuntimeData.Npost[it->nSrc], it->delay, managerRuntimeData.Npost[it->nSrc] - 1, lastDelay);
				}
				lastDelay = it->delay;

				// update the corresponding pre synaptic id
				SynInfo preId = managerRuntimeData.preSynapticIds[pre_pos];
				assert(GET_CONN_NEURON_ID(preId) == it->nSrc);
				//assert(GET_CONN_GRP_ID(preId) == it->grpSrc);
				managerRuntimeData.preSynapticIds[pre_pos] = SET_CONN_ID(it->nSrc, managerRuntimeData.Npost[it->nSrc], it->grpSrc);
				managerRuntimeData.wt[pre_pos] = it->initWt;
				managerRuntimeData.maxSynWt[pre_pos] = it->maxWt;
				managerRuntimeData.connIdsPreIdx[pre_pos] = it->connId;

				managerRuntimeData.Npost[it->nSrc]++;
				parsedConnections++;

				// update the maximum number of and post-connections of a neuron in a group
				if (managerRuntimeData.Npost[it->nSrc] > groupInfo[it->grpSrc].maxPostConn)
					groupInfo[it->grpSrc].maxPostConn = managerRuntimeData.Npost[it->nSrc];
			}
			assert(parsedConnections == managerRuntimeData.Npost[nId]);
			// note: elements in postConnectionList are deallocated automatically with postConnectionList
		}
	}
	assert(connectionLists[0].empty());

	//int p = managerRuntimeData.Npost[src];

	//assert(managerRuntimeData.Npost[src] >= 0);
	//assert(managerRuntimeData.Npre[dest] >= 0);
	//assert((src * maxNumPostSynGrp + p) / numN < maxNumPostSynGrp); // divide by numN to prevent INT overflow

	//unsigned int post_pos = managerRuntimeData.cumulativePost[src] + managerRuntimeData.Npost[src];
	//unsigned int pre_pos  = managerRuntimeData.cumulativePre[dest] + managerRuntimeData.Npre[dest];

	//assert(post_pos < numPostSynNet);
	//assert(pre_pos  < numPreSynNet);

	////generate a new postSynapticIds id for the current connection
	//managerRuntimeData.postSynapticIds[post_pos]   = SET_CONN_ID(dest, managerRuntimeData.Npre[dest], destGrp);
	//tmp_SynapticDelay[post_pos] = dVal;

	//managerRuntimeData.preSynapticIds[pre_pos] = SET_CONN_ID(src, managerRuntimeData.Npost[src], srcGrp);
	//managerRuntimeData.wt[pre_pos] 	  = synWt;
	//managerRuntimeData.maxSynWt[pre_pos] = maxWt;
	//managerRuntimeData.connIdsPreIdx[pre_pos] = connId;

	//bool synWtType = GET_FIXED_PLASTIC(connProp);

	//if (synWtType == SYN_PLASTIC) {
	//	sim_with_fixedwts = false; // if network has any plastic synapses at all, this will be set to true
	//	managerRuntimeData.Npre_plastic[dest]++;
	//	// homeostasis
	//	if (groupConfigs[0][destGrp].WithHomeostasis && groupConfigs[0][destGrp].homeoId ==-1)
	//		groupConfigs[0][destGrp].homeoId = dest; // this neuron info will be printed
	//}

	//managerRuntimeData.Npre[dest] += 1;
	//managerRuntimeData.Npost[src] += 1;

	//groupInfo[srcGrp].numPostConn++;
	//groupInfo[destGrp].numPreConn++;

	//// update the maximum number of pre- and post-connections of a neuron in a group
	//if (managerRuntimeData.Npost[src] > groupInfo[srcGrp].maxPostConn)
	//	groupInfo[srcGrp].maxPostConn = managerRuntimeData.Npost[src];
	//if (managerRuntimeData.Npre[dest] > groupInfo[destGrp].maxPreConn)
	//	groupInfo[destGrp].maxPreConn = managerRuntimeData.Npre[src];
}


void SNN::generatePoissonGroupRuntime(int grpId) {
	for(int i = groupConfigs[0][grpId].StartN; i <= groupConfigs[0][grpId].EndN; i++)
		resetPoissonNeuron(i, grpId);
}

/*!
 * \brief check whether Spike Counters need to be reset
 *
 * A Spike Counter keeps track of all spikes per neuron for a certain time period (recordDur)
 * After this period of time, the spike buffers need to be reset. The trick is to reset it in the very next
 * millisecond, before continuing. For example, if recordDur=1000ms, we want to reset it right before we start
 * executing the 1001st millisecond, so that at t=1000ms the user is able to access non-zero data.
 */
void SNN::checkSpikeCounterRecordDur() {
	for (int g=0; g<numGroups; g++) {
		// skip groups w/o spkMonRT or non-real record durations
		if (!groupConfigs[0][g].withSpikeCounter || groupConfigs[0][g].spkCntRecordDur<=0)
			continue;

		// skip if simTime doesn't need udpating
		// we want to update in spkCntRecordDur + 1, because this function is called rigth at the beginning
		// of each millisecond
		if ( (simTime % ++groupConfigs[0][g].spkCntRecordDurHelper) != 1)
			continue;

 		if (simMode_==GPU_MODE)
			resetSpikeCounter_GPU(g);
		else
			resetSpikeCounter(g);
	}
}


void SNN::collectGlobalNetworkConfig() {
	// find the maximum delay in the network
	// SNN::maxDelay_ is updated
	findMaxDelay(&maxDelay_);
}

// after all the initalization. Its time to create the synaptic weights, weight change and also
// time of firing these are the mostly costly arrays so dense packing is essential to minimize wastage of space
void SNN::compileSNN() {
	KERNEL_DEBUG("Beginning compilation of the network....");

	// compile (update) group and connection configs according to their mutual information
	// update GroupConfig::MaxDelay GroupConfig::FixedInputWts
	// assign GroupConfig::StartN and GroupConfig::EndN
	// Note: MaxDelay, FixedInputWts, StartN, and EndN are invariant in single-GPU or multi-GPUs mode
	compileGroupConfig();

	compileConnectConfig(); // for future use

	// collect the global network config according to compiled gorup and connection configs
	// collect SNN::maxDelay_
	collectGlobalNetworkConfig();

	// perform various consistency checks:
	// - numNeurons vs. sum of all neurons
	// - STDP set on a post-group with incoming plastic connections
	// - etc.
	//verifyNetwork();

	// display the global network configuration
	KERNEL_INFO("\n");
	KERNEL_INFO("************************** Global Network Configuration *******************************");
	KERNEL_INFO("The number of neurons in the network (numN) = %d", numN);
	KERNEL_INFO("The number of regular neurons in the network (numNReg:numNExcReg:numNInhReg) = %d:%d:%d", numNReg, numNExcReg, numNInhReg);
	KERNEL_INFO("The number of poisson neurons in the network (numNPois:numNExcPois:numInhPois) = %d:%d:%d", numNPois, numNExcPois, numNInhPois);
	//KERNEL_INFO("The maximum number of post-connectoins among neurons (maxNumPostSynN) = %d", maxNumPostSynN);
	//KERNEL_INFO("The maximum number of pre-connections among neurons (maxNumPreSynN) = %d", maxNumPreSynN);
	KERNEL_INFO("The maximum axonal delay in the network (maxDelay) = %d", maxDelay_);
	//KERNEL_INFO("The tatol number of synapses in the network (numPreSynNet,numPostSynNet) = (%d,%d)", numPreSynNet, numPostSynNet);

	//ensure that we dont compile the network again
	snnState = COMPILED_SNN;
}

void SNN::compileConnectConfig() {
	// for future  use
}

void SNN::compileGroupConfig() {
	int grpSrc;
	bool synWtType;

	// find the maximum delay for each group according to incoming connection
	for (std::map<int, ConnectConfig>::iterator it = connectConfigMap.begin(); it != connectConfigMap.end(); it++) {
		// check if the current connection's delay meaning grpSrc's delay
		// is greater than the MaxDelay for grpSrc. We find the maximum
		// delay for the grpSrc by this scheme.
		grpSrc = it->second.grpSrc;
		if (it->second.maxDelay > groupConfigMap[grpSrc].MaxDelay)
		 	groupConfigMap[grpSrc].MaxDelay = it->second.maxDelay;

		// given group has plastic connection, and we need to apply STDP rule...
		synWtType = GET_FIXED_PLASTIC(it->second.connProp);
		if (synWtType == SYN_PLASTIC) {
			groupConfigMap[it->second.grpDest].FixedInputWts = false;
		}
	}

	// assigned neruon ids to each group in the order...
	//    !!!!!!! IMPORTANT : NEURON ORGANIZATION/ARRANGEMENT MAP !!!!!!!!!!
	//     <--- Excitatory --> | <-------- Inhibitory REGION ----------> | <-- Excitatory -->
	//     Excitatory-Regular  | Inhibitory-Regular | Inhibitory-Poisson | Excitatory-Poisson
	int assignedGroup = 0;
	int availableNeuronId = 0;
	for(int order = 0; order < 4; order++) {
		for(int g = 0; g < numGroups; g++) {
			if (IS_EXCITATORY_TYPE(groupConfigMap[g].Type) && (groupConfigMap[g].Type & POISSON_NEURON) && order == 3) {
				availableNeuronId = assignGroup(g, availableNeuronId);
				assignedGroup++;
			} else if (IS_INHIBITORY_TYPE(groupConfigMap[g].Type) &&  (groupConfigMap[g].Type & POISSON_NEURON) && order == 2) {
				availableNeuronId = assignGroup(g, availableNeuronId);
				assignedGroup++;
			} else if (IS_EXCITATORY_TYPE(groupConfigMap[g].Type) && !(groupConfigMap[g].Type & POISSON_NEURON) && order == 0) {
				availableNeuronId = assignGroup(g, availableNeuronId);
				assignedGroup++;
			} else if (IS_INHIBITORY_TYPE(groupConfigMap[g].Type) && !(groupConfigMap[g].Type & POISSON_NEURON) && order == 1) {
				availableNeuronId = assignGroup(g, availableNeuronId);
				assignedGroup++;
			}
		}
	}
	assert(availableNeuronId == numN);
	assert(assignedGroup == numGroups);
}

void SNN::connectNetwork() {
	// this parse generates local connections
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		for (std::list<ConnectConfig>::iterator connIt = localConnectLists[netId].begin(); connIt != localConnectLists[netId].end(); connIt++) {
			switch(connIt->type) {
				case CONN_RANDOM:
					connectRandom(netId, connIt, false);
					break;
				case CONN_FULL:
					connectFull(netId, connIt, false);
					break;
				case CONN_FULL_NO_DIRECT:
					connectFull(netId, connIt, false);
					break;
				case CONN_ONE_TO_ONE:
					connectOneToOne(netId, connIt, false);
					break;
				case CONN_GAUSSIAN:
					connectGaussian(netId, connIt, false);
					break;
				case CONN_USER_DEFINED:
					connectUserDefined(netId, connIt, false);
					break;
				default:
					KERNEL_ERROR("Invalid connection type( should be 'random', 'full', 'full-no-direct', or 'one-to-one')");
					exitSimulation(-1);
			}
		}
	}

	// this parse generates external connections
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		for (std::list<ConnectConfig>::iterator connIt = externalConnectLists[netId].begin(); connIt != externalConnectLists[netId].end(); connIt++) {
			switch(connIt->type) {
				case CONN_RANDOM:
					connectRandom(netId, connIt, true);
					break;
				case CONN_FULL:
					connectFull(netId, connIt, true);
					break;
				case CONN_FULL_NO_DIRECT:
					connectFull(netId, connIt, true);
					break;
				case CONN_ONE_TO_ONE:
					connectOneToOne(netId, connIt, true);
					break;
				case CONN_GAUSSIAN:
					connectGaussian(netId, connIt, true);
					break;
				case CONN_USER_DEFINED:
					connectUserDefined(netId, connIt, true);
					break;
				default:
					KERNEL_ERROR("Invalid connection type( should be 'random', 'full', 'full-no-direct', or 'one-to-one')");
					exitSimulation(-1);
			}
		}
	}
}

//! set one specific connection from neuron id 'src' to neuron id 'dest'
inline void SNN::connectNeurons(int netId, int _grpSrc, int _grpDest, int _nSrc, int _nDest, short int _connId, int externalNetId) {
	//assert(destN <= CONN_SYN_NEURON_MASK); // total number of neurons is less than 1 million within a GPU
	ConnectionInfo connInfo;
	connInfo.grpSrc = _grpSrc;
	connInfo.grpDest = _grpDest;
	connInfo.nSrc = _nSrc;
	connInfo.nDest = _nDest;
	connInfo.connId = _connId;
	connInfo.preSynId = -1;
	connInfo.initWt = 0.0f;
	connInfo.maxWt = 0.0f;
	connInfo.delay = 0;

	connectionLists[netId].push_back(connInfo);

	if (externalNetId >= 0)
		connectionLists[externalNetId].push_back(connInfo);
}

// make 'C' full connections from grpSrc to grpDest
void SNN::connectFull(int netId, std::list<ConnectConfig>::iterator connIt, bool isExternal) {
	int grpSrc = connIt->grpSrc;
	int grpDest = connIt->grpDest;
	bool noDirect = (connIt->type == CONN_FULL_NO_DIRECT);
	int externalNetId = -1;

	if (isExternal) {
		externalNetId = groupConfigMap[grpDest].netId;
		assert(netId != externalNetId);
	}

	// rebuild struct for easier handling
	RadiusRF radius(connIt->radX, connIt->radY, connIt->radZ);

	for(int i = groupConfigMap[grpSrc].StartN; i <= groupConfigMap[grpSrc].EndN; i++)  {
		//Point3D loc_i = getNeuronLocation3D(i); // 3D coordinates of i
		for(int j = groupConfigMap[grpDest].StartN; j <= groupConfigMap[grpDest].EndN; j++) { // j: the temp neuron id
			// if flag is set, don't connect direct connections
			if((noDirect) && (i - groupConfigMap[grpSrc].StartN) == (j - groupConfigMap[grpDest].StartN))
				continue;

			// check whether pre-neuron location is in RF of post-neuron
			//Point3D loc_j = getNeuronLocation3D(j); // 3D coordinates of j
			//if (!isPoint3DinRF(radius, loc_i, loc_j))
			//	continue;

			connectNeurons(netId, grpSrc, grpDest, i, j, connIt->connId, externalNetId);
			connIt->numberOfConnections++;
		}
	}

	std::list<GroupConfigRT>::iterator grpIt;
	GroupConfigRT targetGrp;

	// update numPostSynapses and numPreSynapses of groups in the local network
	targetGrp.grpId = grpSrc; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPostSynapses += connIt->numberOfConnections;

	targetGrp.grpId = grpDest; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPreSynapses += connIt->numberOfConnections;
	
	// also update numPostSynapses and numPreSynapses of groups in the external network if the connection is external
	if (isExternal) {
		targetGrp.grpId = grpSrc; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPostSynapses += connIt->numberOfConnections;

		targetGrp.grpId = grpDest; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPreSynapses += connIt->numberOfConnections;
	}
}

void SNN::connectGaussian(int netId, std::list<ConnectConfig>::iterator connIt, bool isExternal) {
	// rebuild struct for easier handling
	// adjust with sqrt(2) in order to make the Gaussian kernel depend on 2*sigma^2
	RadiusRF radius(connIt->radX, connIt->radY, connIt->radZ);

	// in case pre and post have different Grid3D sizes: scale pre to the grid size of post
	int grpSrc = connIt->grpSrc;
	int grpDest = connIt->grpDest;
	//Grid3D grid_i = getGroupGrid3D(grpSrc);
	//Grid3D grid_j = getGroupGrid3D(grpDest);
	//Point3D scalePre = Point3D(grid_j.numX, grid_j.numY, grid_j.numZ) / Point3D(grid_i.numX, grid_i.numY, grid_i.numZ);
	int externalNetId = -1;

	if (isExternal) {
		externalNetId = groupConfigMap[grpDest].netId;
		assert(netId != externalNetId);
	}

	for(int i = groupConfigMap[grpSrc].StartN; i <= groupConfigMap[grpSrc].EndN; i++)  {
		//Point3D loc_i = getNeuronLocation3D(i)*scalePre; // i: adjusted 3D coordinates

		for(int j = groupConfigMap[grpDest].StartN; j <= groupConfigMap[grpDest].EndN; j++) { // j: the temp neuron id
			// check whether pre-neuron location is in RF of post-neuron
			//Point3D loc_j = getNeuronLocation3D(j); // 3D coordinates of j

			// make sure point is in RF
			//double rfDist = getRFDist3D(radius,loc_i,loc_j);
			//if (rfDist < 0.0 || rfDist > 1.0)
			//	continue;

			// if rfDist is valid, it returns a number between 0 and 1
			// we want these numbers to fit to Gaussian weigths, so that rfDist=0 corresponds to max Gaussian weight
			// and rfDist=1 corresponds to 0.1 times max Gaussian weight
			// so we're looking at gauss = exp(-a*rfDist), where a such that exp(-a)=0.1
			// solving for a, we find that a = 2.3026
			//double gauss = exp(-2.3026*rfDist);
			//if (gauss < 0.1)
			//	continue;

			if (drand48() < connIt->p) {
				connectNeurons(netId, grpSrc, grpDest, i, j, connIt->connId, externalNetId);
				connIt->numberOfConnections++;
			}
		}
	}

	std::list<GroupConfigRT>::iterator grpIt;
	GroupConfigRT targetGrp;

	// update numPostSynapses and numPreSynapses of groups in the local network
	targetGrp.grpId = grpSrc; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPostSynapses += connIt->numberOfConnections;

	targetGrp.grpId = grpDest; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPreSynapses += connIt->numberOfConnections;
	
	// also update numPostSynapses and numPreSynapses of groups in the external network if the connection is external
	if (isExternal) {
		targetGrp.grpId = grpSrc; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPostSynapses += connIt->numberOfConnections;

		targetGrp.grpId = grpDest; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPreSynapses += connIt->numberOfConnections;
	}
}

void SNN::connectOneToOne(int netId, std::list<ConnectConfig>::iterator connIt, bool isExternal) {
	int grpSrc = connIt->grpSrc;
	int grpDest = connIt->grpDest;
	int externalNetId = -1;

	if (isExternal) {
		externalNetId = groupConfigMap[grpDest].netId;
		assert(netId != externalNetId);
	}

	assert( groupConfigMap[grpDest].SizeN == groupConfigMap[grpSrc].SizeN );

	// NOTE: RadiusRF does not make a difference here: ignore
	for(int nid = groupConfigMap[grpSrc].StartN, j = groupConfigMap[grpDest].StartN; nid <= groupConfigMap[grpSrc].EndN; nid++, j++)  {
		connectNeurons(netId, grpSrc, grpDest, nid, j, connIt->connId, externalNetId);
		connIt->numberOfConnections++;
	}

	std::list<GroupConfigRT>::iterator grpIt;
	GroupConfigRT targetGrp;

	// update numPostSynapses and numPreSynapses of groups in the local network
	targetGrp.grpId = grpSrc; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPostSynapses += connIt->numberOfConnections;

	targetGrp.grpId = grpDest; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPreSynapses += connIt->numberOfConnections;
	
	// also update numPostSynapses and numPreSynapses of groups in the external network if the connection is external
	if (isExternal) {
		targetGrp.grpId = grpSrc; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPostSynapses += connIt->numberOfConnections;

		targetGrp.grpId = grpDest; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPreSynapses += connIt->numberOfConnections;
	}
}

// make 'C' random connections from grpSrc to grpDest
void SNN::connectRandom(int netId, std::list<ConnectConfig>::iterator connIt, bool isExternal) {
	int grpSrc = connIt->grpSrc;
	int grpDest = connIt->grpDest;
	int externalNetId = -1;

	if (isExternal) {
		externalNetId = groupConfigMap[grpDest].netId;
		assert(netId != externalNetId);
	}

	// rebuild struct for easier handling
	//RadiusRF radius(connectConfigMap[connId].radX, connectConfigMap[connId].radY, connectConfigMap[connId].radZ);

	for(int pre_nid = groupConfigMap[grpSrc].StartN; pre_nid <= groupConfigMap[grpSrc].EndN; pre_nid++) {
		//Point3D loc_pre = getNeuronLocation3D(pre_nid); // 3D coordinates of i
		for(int post_nid = groupConfigMap[grpDest].StartN; post_nid <= groupConfigMap[grpDest].EndN; post_nid++) {
			// check whether pre-neuron location is in RF of post-neuron
			//Point3D loc_post = getNeuronLocation3D(post_nid); // 3D coordinates of j
			//if (!isPoint3DinRF(radius, loc_pre, loc_post))
			//	continue;

			if (drand48() < connIt->p) {
				connectNeurons(netId, grpSrc, grpDest, pre_nid, post_nid, connIt->connId, externalNetId);
				connIt->numberOfConnections++;
			}
		}
	}

	std::list<GroupConfigRT>::iterator grpIt;
	GroupConfigRT targetGrp;

	// update numPostSynapses and numPreSynapses of groups in the local network
	targetGrp.grpId = grpSrc; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPostSynapses += connIt->numberOfConnections;

	targetGrp.grpId = grpDest; // the other fields does not matter
	grpIt = std::find(groupPartitionLists[netId].begin(), groupPartitionLists[netId].end(), targetGrp);
	assert(grpIt != groupPartitionLists[netId].end());
	grpIt->numPreSynapses += connIt->numberOfConnections;
	
	// also update numPostSynapses and numPreSynapses of groups in the external network if the connection is external
	if (isExternal) {
		targetGrp.grpId = grpSrc; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPostSynapses += connIt->numberOfConnections;

		targetGrp.grpId = grpDest; // the other fields does not matter
		grpIt = std::find(groupPartitionLists[externalNetId].begin(), groupPartitionLists[externalNetId].end(), targetGrp);
		assert(grpIt != groupPartitionLists[externalNetId].end());
		grpIt->numPreSynapses += connIt->numberOfConnections;
	}
}

// FIXME: rewrite user-define call-back function
// user-defined functions called here...
// This is where we define our user-defined call-back function.  -- KDC
void SNN::connectUserDefined(int netId, std::list<ConnectConfig>::iterator connIt, bool isExternal) {
	//int grpSrc = connectConfigMap[connId].grpSrc;
	//int grpDest = connectConfigMap[connId].grpDest;
	//connectConfigMap[connId].maxDelay = 0;
	//for(int nid=groupConfigMap[grpSrc].StartN; nid<=groupConfigMap[grpSrc].EndN; nid++) {
	//	for(int nid2=groupConfigMap[grpDest].StartN; nid2 <= groupConfigMap[grpDest].EndN; nid2++) {
	//		int srcId  = nid  - groupConfigMap[grpSrc].StartN;
	//		int destId = nid2 - groupConfigMap[grpDest].StartN;
	//		float weight, maxWt, delay;
	//		bool connected;

	//		connectConfigMap[connId].conn->connect(this, grpSrc, srcId, grpDest, destId, weight, maxWt, delay, connected);
	//		if(connected)  {
	//			if (GET_FIXED_PLASTIC(connectConfigMap[connId].connProp) == SYN_FIXED)
	//				maxWt = weight;

	//			connectConfigMap[connId].maxWt = maxWt;

	//			assert(delay >= 1);
	//			assert(delay <= MAX_SYN_DELAY);
	//			assert(abs(weight) <= abs(maxWt));

	//			// adjust the sign of the weight based on inh/exc connection
	//			weight = isExcitatoryGroup(grpSrc) ? fabs(weight) : -1.0*fabs(weight);
	//			maxWt  = isExcitatoryGroup(grpSrc) ? fabs(maxWt)  : -1.0*fabs(maxWt);

	//			setConnection(grpSrc, grpDest, nid, nid2, weight, maxWt, delay, connectConfigMap[connId].connProp, connId);// info->connId);
	//			connectConfigMap[connId].numberOfConnections++;
	//			if(delay > connectConfigMap[connId].maxDelay) {
	//				connectConfigMap[connId].maxDelay = delay;
	//			}
	//		}
	//	}
	//}

	//groupInfo[grpSrc].sumPostConn += connectConfigMap[connId].numberOfConnections;
	//groupInfo[grpDest].sumPreConn += connectConfigMap[connId].numberOfConnections;
}

//// make 'C' full connections from grpSrc to grpDest
//void SNN::connectFull(short int connId) {
//	int grpSrc = connectConfigMap[connId].grpSrc;
//	int grpDest = connectConfigMap[connId].grpDest;
//	bool noDirect = (connectConfigMap[connId].type == CONN_FULL_NO_DIRECT);
//
//	// rebuild struct for easier handling
//	RadiusRF radius(connectConfigMap[connId].radX, connectConfigMap[connId].radY, connectConfigMap[connId].radZ);
//
//	for(int i = groupConfigMap[grpSrc].StartN; i <= groupConfigMap[grpSrc].EndN; i++)  {
//		Point3D loc_i = getNeuronLocation3D(i); // 3D coordinates of i
//		for(int j = groupConfigMap[grpDest].StartN; j <= groupConfigMap[grpDest].EndN; j++) { // j: the temp neuron id
//			// if flag is set, don't connect direct connections
//			if((noDirect) && (i - groupConfigMap[grpSrc].StartN) == (j - groupConfigMap[grpDest].StartN))
//				continue;
//
//			// check whether pre-neuron location is in RF of post-neuron
//			Point3D loc_j = getNeuronLocation3D(j); // 3D coordinates of j
//			if (!isPoint3DinRF(radius, loc_i, loc_j))
//				continue;
//
//			//uint8_t dVal = info->minDelay + (int)(0.5 + (drand48() * (info->maxDelay - info->minDelay)));
//			uint8_t dVal = connectConfigMap[connId].minDelay + rand() % (connectConfigMap[connId].maxDelay - connectConfigMap[connId].minDelay + 1);
//			assert((dVal >= connectConfigMap[connId].minDelay) && (dVal <= connectConfigMap[connId].maxDelay));
//			float synWt = generateWeight(connectConfigMap[connId].connProp, connectConfigMap[connId].initWt, connectConfigMap[connId].maxWt, i, grpSrc);
//
//			setConnection(grpSrc, grpDest, i, j, synWt, connectConfigMap[connId].maxWt, dVal, connectConfigMap[connId].connProp, connId);// info->connId);
//			connectConfigMap[connId].numberOfConnections++;
//		}
//	}
//
//	groupInfo[grpSrc].sumPostConn += connectConfigMap[connId].numberOfConnections;
//	groupInfo[grpDest].sumPreConn += connectConfigMap[connId].numberOfConnections;
//}

//void SNN::connectGaussian(short int connId) {
//	// rebuild struct for easier handling
//	// adjust with sqrt(2) in order to make the Gaussian kernel depend on 2*sigma^2
//	RadiusRF radius(connectConfigMap[connId].radX, connectConfigMap[connId].radY, connectConfigMap[connId].radZ);
//
//	// in case pre and post have different Grid3D sizes: scale pre to the grid size of post
//	int grpSrc = connectConfigMap[connId].grpSrc;
//	int grpDest = connectConfigMap[connId].grpDest;
//	Grid3D grid_i = getGroupGrid3D(grpSrc);
//	Grid3D grid_j = getGroupGrid3D(grpDest);
//	Point3D scalePre = Point3D(grid_j.numX, grid_j.numY, grid_j.numZ) / Point3D(grid_i.numX, grid_i.numY, grid_i.numZ);
//
//	for(int i = groupConfigMap[grpSrc].StartN; i <= groupConfigMap[grpSrc].EndN; i++)  {
//		Point3D loc_i = getNeuronLocation3D(i)*scalePre; // i: adjusted 3D coordinates
//
//		for(int j = groupConfigMap[grpDest].StartN; j <= groupConfigMap[grpDest].EndN; j++) { // j: the temp neuron id
//			// check whether pre-neuron location is in RF of post-neuron
//			Point3D loc_j = getNeuronLocation3D(j); // 3D coordinates of j
//
//			// make sure point is in RF
//			double rfDist = getRFDist3D(radius,loc_i,loc_j);
//			if (rfDist < 0.0 || rfDist > 1.0)
//				continue;
//
//			// if rfDist is valid, it returns a number between 0 and 1
//			// we want these numbers to fit to Gaussian weigths, so that rfDist=0 corresponds to max Gaussian weight
//			// and rfDist=1 corresponds to 0.1 times max Gaussian weight
//			// so we're looking at gauss = exp(-a*rfDist), where a such that exp(-a)=0.1
//			// solving for a, we find that a = 2.3026
//			double gauss = exp(-2.3026*rfDist);
//			if (gauss < 0.1)
//				continue;
//
//			if (drand48() < connectConfigMap[connId].p) {
//				uint8_t dVal = connectConfigMap[connId].minDelay + rand() % (connectConfigMap[connId].maxDelay - connectConfigMap[connId].minDelay + 1);
//				assert((dVal >= connectConfigMap[connId].minDelay) && (dVal <= connectConfigMap[connId].maxDelay));
//				float synWt = gauss * connectConfigMap[connId].initWt; // scale weight according to gauss distance
//				setConnection(grpSrc, grpDest, i, j, synWt, connectConfigMap[connId].maxWt, dVal, connectConfigMap[connId].connProp, connId);//info->connId);
//				connectConfigMap[connId].numberOfConnections++;
//			}
//		}
//	}
//
//	groupInfo[grpSrc].sumPostConn += connectConfigMap[connId].numberOfConnections;
//	groupInfo[grpDest].sumPreConn += connectConfigMap[connId].numberOfConnections;
//}
//
//void SNN::connectOneToOne(short int connId) {
//	int grpSrc = connectConfigMap[connId].grpSrc;
//	int grpDest = connectConfigMap[connId].grpDest;
//	assert( groupConfigMap[grpDest].SizeN == groupConfigMap[grpSrc].SizeN );
//
//	// NOTE: RadiusRF does not make a difference here: ignore
//	for(int nid=groupConfigMap[grpSrc].StartN,j=groupConfigMap[grpDest].StartN; nid<=groupConfigMap[grpSrc].EndN; nid++, j++)  {
//		uint8_t dVal = connectConfigMap[connId].minDelay + rand() % (connectConfigMap[connId].maxDelay - connectConfigMap[connId].minDelay + 1);
//		assert((dVal >= connectConfigMap[connId].minDelay) && (dVal <= connectConfigMap[connId].maxDelay));
//		float synWt = generateWeight(connectConfigMap[connId].connProp, connectConfigMap[connId].initWt, connectConfigMap[connId].maxWt, nid, grpSrc);
//		setConnection(grpSrc, grpDest, nid, j, synWt, connectConfigMap[connId].maxWt, dVal, connectConfigMap[connId].connProp, connId);//info->connId);
//		connectConfigMap[connId].numberOfConnections++;
//	}
//
//	groupInfo[grpSrc].sumPostConn += connectConfigMap[connId].numberOfConnections;
//	groupInfo[grpDest].sumPreConn += connectConfigMap[connId].numberOfConnections;
//}
//
//// make 'C' random connections from grpSrc to grpDest
//void SNN::connectRandom(short int connId) {
//	int grpSrc = connectConfigMap[connId].grpSrc;
//	int grpDest = connectConfigMap[connId].grpDest;
//
//	// rebuild struct for easier handling
//	RadiusRF radius(connectConfigMap[connId].radX, connectConfigMap[connId].radY, connectConfigMap[connId].radZ);
//
//	for(int pre_nid = groupConfigMap[grpSrc].StartN; pre_nid <= groupConfigMap[grpSrc].EndN; pre_nid++) {
//		Point3D loc_pre = getNeuronLocation3D(pre_nid); // 3D coordinates of i
//		for(int post_nid = groupConfigMap[grpDest].StartN; post_nid <= groupConfigMap[grpDest].EndN; post_nid++) {
//			// check whether pre-neuron location is in RF of post-neuron
//			Point3D loc_post = getNeuronLocation3D(post_nid); // 3D coordinates of j
//			if (!isPoint3DinRF(radius, loc_pre, loc_post))
//				continue;
//
//			if (drand48() < connectConfigMap[connId].p) {
//				//uint8_t dVal = info->minDelay + (int)(0.5+(drand48()*(info->maxDelay-info->minDelay)));
//				uint8_t dVal = connectConfigMap[connId].minDelay + rand() % (connectConfigMap[connId].maxDelay - connectConfigMap[connId].minDelay + 1);
//				assert((dVal >= connectConfigMap[connId].minDelay) && (dVal <= connectConfigMap[connId].maxDelay));
//				float synWt = generateWeight(connectConfigMap[connId].connProp, connectConfigMap[connId].initWt, connectConfigMap[connId].maxWt, pre_nid, grpSrc);
//				setConnection(grpSrc, grpDest, pre_nid, post_nid, synWt, connectConfigMap[connId].maxWt, dVal, connectConfigMap[connId].connProp, connId); //info->connId);
//				connectConfigMap[connId].numberOfConnections++;
//			}
//		}
//	}
//
//	groupInfo[grpSrc].sumPostConn += connectConfigMap[connId].numberOfConnections;
//	groupInfo[grpDest].sumPreConn += connectConfigMap[connId].numberOfConnections;
//}
//
//// user-defined functions called here...
//// This is where we define our user-defined call-back function.  -- KDC
//void SNN::connectUserDefined(short int connId) {
//	int grpSrc = connectConfigMap[connId].grpSrc;
//	int grpDest = connectConfigMap[connId].grpDest;
//	connectConfigMap[connId].maxDelay = 0;
//	for(int nid=groupConfigMap[grpSrc].StartN; nid<=groupConfigMap[grpSrc].EndN; nid++) {
//		for(int nid2=groupConfigMap[grpDest].StartN; nid2 <= groupConfigMap[grpDest].EndN; nid2++) {
//			int srcId  = nid  - groupConfigMap[grpSrc].StartN;
//			int destId = nid2 - groupConfigMap[grpDest].StartN;
//			float weight, maxWt, delay;
//			bool connected;
//
//			connectConfigMap[connId].conn->connect(this, grpSrc, srcId, grpDest, destId, weight, maxWt, delay, connected);
//			if(connected)  {
//				if (GET_FIXED_PLASTIC(connectConfigMap[connId].connProp) == SYN_FIXED)
//					maxWt = weight;
//
//				connectConfigMap[connId].maxWt = maxWt;
//
//				assert(delay >= 1);
//				assert(delay <= MAX_SYN_DELAY);
//				assert(abs(weight) <= abs(maxWt));
//
//				// adjust the sign of the weight based on inh/exc connection
//				weight = isExcitatoryGroup(grpSrc) ? fabs(weight) : -1.0*fabs(weight);
//				maxWt  = isExcitatoryGroup(grpSrc) ? fabs(maxWt)  : -1.0*fabs(maxWt);
//
//				setConnection(grpSrc, grpDest, nid, nid2, weight, maxWt, delay, connectConfigMap[connId].connProp, connId);// info->connId);
//				connectConfigMap[connId].numberOfConnections++;
//				if(delay > connectConfigMap[connId].maxDelay) {
//					connectConfigMap[connId].maxDelay = delay;
//				}
//			}
//		}
//	}
//
//	groupInfo[grpSrc].sumPostConn += connectConfigMap[connId].numberOfConnections;
//	groupInfo[grpDest].sumPreConn += connectConfigMap[connId].numberOfConnections;
//}

// delete all objects (CPU and GPU side)
void SNN::deleteObjects() {
	if (simulatorDeleted)
		return;

	printSimSummary();

	// fclose file streams, unless in custom mode
	if (loggerMode_ != CUSTOM) {
		// don't fclose if it's stdout or stderr, otherwise they're gonna stay closed for the rest of the process
		if (fpInf_!=NULL && fpInf_!=stdout && fpInf_!=stderr)
			fclose(fpInf_);
		if (fpErr_!=NULL && fpErr_!=stdout && fpErr_!=stderr)
			fclose(fpErr_);
		if (fpDeb_!=NULL && fpDeb_!=stdout && fpDeb_!=stderr)
			fclose(fpDeb_);
		if (fpLog_!=NULL && fpLog_!=stdout && fpLog_!=stderr)
			fclose(fpLog_);
	}

	// deallocate objects
	resetMonitors(true);
	resetConnectionConfigs(true);
	resetRuntimeData(true);

	// do the same as above, but for snn_gpu.cu
	deleteObjects_GPU();
	simulatorDeleted = true;
}

int SNN::findGrpId(int nid) {
	KERNEL_WARN("Using findGrpId is deprecated, use array grpIds[] instead...");
	for(int g=0; g < numGroups; g++) {
		if(nid >=groupConfigs[0][g].StartN && (nid <=groupConfigs[0][g].EndN)) {
			return g;
		}
	}
	KERNEL_ERROR("findGrp(): cannot find the group for neuron %d", nid);
	exitSimulation(1);
}

void SNN::findMaxDelay(int* _maxDelay) {
	int grpSrc;
	*_maxDelay = 0;
	// scall all connect configs to find the maximum delay in the network
	for (std::map<int, ConnectConfig>::iterator it = connectConfigMap.begin(); it != connectConfigMap.end(); it++) {
		grpSrc = it->second.grpSrc;
		if (it->second.maxDelay > *_maxDelay)
			*_maxDelay = it->second.maxDelay;
	}
}

void SNN::findMaxNumSynapsesGroups(int* _maxNumPostSynGrp, int* _maxNumPreSynGrp) {
	*_maxNumPostSynGrp = 0;
	*_maxNumPreSynGrp = 0;

	//  scan all the groups and find the required information
	for (int g = 0; g < numGroups; g++) {
		// find the values for maximum postsynaptic length
		// and maximum pre-synaptic length
		if (groupConfigMap[g].numPostSynapses > *_maxNumPostSynGrp)
			*_maxNumPostSynGrp = groupConfigMap[g].numPostSynapses;
		if (groupConfigMap[g].numPreSynapses > *_maxNumPreSynGrp)
			*_maxNumPreSynGrp = groupConfigMap[g].numPreSynapses;
	}
}

void SNN::findMaxNumSynapsesNeurons(int _netId, int& _maxNumPostSynN, int& _maxNumPreSynN) {
	int *tempNpre, *tempNpost;
	int nSrc, nDest, numNeurons;
	std::map<int, int> globalToLocalOffset;

	numNeurons = networkConfigs[_netId].numN;
	tempNpre = new int[numNeurons];
	tempNpost = new int[numNeurons];
	memset(tempNpre, 0, sizeof(int) * numNeurons);
	memset(tempNpost, 0, sizeof(int) * numNeurons);

	// load offset between global neuron id and local neuron id 
	for (std::list<GroupConfigRT>::iterator grpIt = groupPartitionLists[_netId].begin(); grpIt != groupPartitionLists[_netId].end(); grpIt++) {
		globalToLocalOffset[grpIt->grpId] = grpIt->GtoLOffset;
	}

	// calculate number of pre- and post- connections of each neuron
	for (std::list<ConnectionInfo>::iterator connIt = connectionLists[_netId].begin(); connIt != connectionLists[_netId].end(); connIt++) {
		nSrc = connIt->nSrc + globalToLocalOffset[connIt->grpSrc];
		nDest = connIt->nDest + globalToLocalOffset[connIt->grpDest];
		assert(nSrc < numNeurons); assert(nDest < numNeurons);
		tempNpost[nSrc]++;
		tempNpre[nDest]++;
	}

	// find out the maximum number of pre- and post- connections among neurons in a local network
	_maxNumPostSynN = 0;
	_maxNumPreSynN = 0;
	for (int nId = 0; nId < networkConfigs[_netId].numN; nId++) {
		if (tempNpost[nId] > _maxNumPostSynN) _maxNumPostSynN = tempNpost[nId];
		if (tempNpre[nId] > _maxNumPreSynN) _maxNumPreSynN = tempNpre[nId];
	}

	delete [] tempNpre;
	delete [] tempNpost;
}

void SNN::findMaxSpikesD1D2(int _netId, unsigned int& _maxSpikesD1, unsigned int& _maxSpikesD2) {
	_maxSpikesD1 = 0; _maxSpikesD2 = 0;
	for(std::list<GroupConfigRT>::iterator grpIt = groupPartitionLists[_netId].begin(); grpIt != groupPartitionLists[_netId].end(); grpIt++) {
		if (grpIt->MaxDelay == 1)
			_maxSpikesD1 += (grpIt->SizeN * grpIt->MaxFiringRate);
		else
			_maxSpikesD2 += (grpIt->SizeN * grpIt->MaxFiringRate);
	}
}

void SNN::findNumN(int _netId, int& _numN, int& _numNExternal, int& _numNAssigned,
                   int& _numNReg, int& _numNExcReg, int& _numNInhReg,
                   int& _numNPois, int& _numNExcPois, int& _numNInhPois) {
	_numN = 0; _numNExternal = 0; _numNAssigned = 0;
	_numNReg = 0; _numNExcReg = 0; _numNInhReg = 0;
	_numNPois = 0; _numNExcPois = 0; _numNInhPois = 0;
	for (std::list<GroupConfigRT>::iterator grpIt = groupPartitionLists[_netId].begin(); grpIt != groupPartitionLists[_netId].end(); grpIt++) {
		int sizeN = grpIt->SizeN;
		if (IS_EXCITATORY_TYPE(grpIt->Type) && (grpIt->Type & POISSON_NEURON) && grpIt->netId == _netId) {
			_numN += sizeN; _numNPois += sizeN; _numNExcPois += sizeN;
		} else if (IS_INHIBITORY_TYPE(grpIt->Type) && (grpIt->Type & POISSON_NEURON) && grpIt->netId == _netId) {
			_numN += sizeN; _numNPois += sizeN; _numNInhPois += sizeN;
		} else if (IS_EXCITATORY_TYPE(grpIt->Type) && !(grpIt->Type & POISSON_NEURON) && grpIt->netId == _netId) {
			_numN += sizeN; _numNReg += sizeN; _numNExcReg += sizeN;
		} else if (IS_INHIBITORY_TYPE(grpIt->Type) && !(grpIt->Type & POISSON_NEURON) && grpIt->netId == _netId) {
			_numN += sizeN; _numNReg += sizeN; _numNInhReg += sizeN;
		} else if (grpIt->netId != _netId) {
			_numNExternal += grpIt->SizeN;
		} else {
			KERNEL_ERROR("Can't find catagory for the group [%d] ", grpIt->grpId);
			exitSimulation(-1);
		}
		_numNAssigned += sizeN;
	}

	assert(_numNReg == _numNExcReg + _numNInhReg);
	assert(_numNPois == _numNExcPois + _numNInhPois);
	assert(_numN == _numNReg + _numNPois);
	assert(_numNAssigned == _numN + _numNExternal);
}

void SNN::findNumSynapsesNetwork(int _netId, int& _numPostSynNet, int& _numPreSynNet) {
	_numPostSynNet = 0;
	_numPreSynNet  = 0;

	for (std::list<GroupConfigRT>::iterator grpIt = groupPartitionLists[_netId].begin(); grpIt != groupPartitionLists[_netId].end(); grpIt++) {
		_numPostSynNet += grpIt->numPostSynapses;
		_numPreSynNet += grpIt->numPreSynapses;
		assert(_numPostSynNet < INT_MAX);
		assert(_numPreSynNet <  INT_MAX);
	}

	assert(_numPreSynNet == _numPostSynNet);
}

inline int SNN::getPoissNeuronPos(int nid) {
	int nPos = nid-numNReg;
	assert(nid >= numNReg);
	assert(nid < numN);
	assert((nid-numNReg) < numNPois);
	return nPos;
}

//We need pass the neuron id (nid) and the grpId just for the case when we want to
//ramp up/down the weights.  In that case we need to set the weights of each synapse
//depending on their nid (their position with respect to one another). -- KDC
float SNN::generateWeight(int connProp, float initWt, float maxWt, int nid, int grpId) {
	float actWts;
	// \FIXME: are these ramping thingies still supported?
	bool setRandomWeights   = GET_INITWTS_RANDOM(connProp);
	bool setRampDownWeights = GET_INITWTS_RAMPDOWN(connProp);
	bool setRampUpWeights   = GET_INITWTS_RAMPUP(connProp);

	if (setRandomWeights)
		actWts = initWt * drand48();
	else if (setRampUpWeights)
		actWts = (initWt + ((nid - groupConfigs[0][grpId].StartN) * (maxWt - initWt) / groupConfigs[0][grpId].SizeN));
	else if (setRampDownWeights)
		actWts = (maxWt - ((nid - groupConfigs[0][grpId].StartN) * (maxWt - initWt) / groupConfigs[0][grpId].SizeN));
	else
		actWts = initWt;

	return actWts;
}

/*!
 * \brief This function initialize variables in GroupConfigRT to fail-safe vaule
 */
void SNN::initGroupConfig(GroupConfigRT* _groupConfig) {
	// global idenfications
	_groupConfig->netId = -1;
	_groupConfig->grpId = -1;
	_groupConfig->StartN = -1;
	_groupConfig->EndN = -1;

	// local idenfications
	_groupConfig->localGrpId = -1;
	_groupConfig->localStartN = -1;
	_groupConfig->localEndN = -1;

	_groupConfig->LtoGOffset = 0;
	_groupConfig->GtoLOffset = 0;

	_groupConfig->Type = UNKNOWN_NEURON;
	_groupConfig->MaxFiringRate = UNKNOWN_NEURON_MAX_FIRING_RATE;
	_groupConfig->SpikeMonitorId = -1;
	_groupConfig->GroupMonitorId = -1;
	_groupConfig->numPostSynapses = 0;	// default value
	_groupConfig->numPreSynapses = 0;	// default value
	_groupConfig->WithSTP = false;
	_groupConfig->WithSTDP = false;
	_groupConfig->WithESTDP = false;
	_groupConfig->WithISTDP = false;
	_groupConfig->WithESTDPtype = UNKNOWN_STDP;
	_groupConfig->WithISTDPtype = UNKNOWN_STDP;
	_groupConfig->WithESTDPcurve = UNKNOWN_CURVE;
	_groupConfig->WithISTDPcurve = UNKNOWN_CURVE;
	_groupConfig->FixedInputWts = true; // Default is true. This value changed to false
	// if any incoming  connections are plastic
	_groupConfig->isSpikeGenerator = false;
	_groupConfig->RatePtr = NULL;

	_groupConfig->homeoId = -1;
	_groupConfig->avgTimeScale  = 10000.0;

	_groupConfig->baseDP = 1.0f;
	_groupConfig->base5HT = 1.0f;
	_groupConfig->baseACh = 1.0f;
	_groupConfig->baseNE = 1.0f;
	_groupConfig->decayDP = 1 - (1.0f / 100);
	_groupConfig->decay5HT = 1 - (1.0f / 100);
	_groupConfig->decayACh = 1 - (1.0f / 100);
	_groupConfig->decayNE = 1 - (1.0f / 100);

	_groupConfig->spikeGen = NULL;

	_groupConfig->withSpikeCounter = false;
	_groupConfig->spkCntRecordDur = -1;
	_groupConfig->spkCntRecordDurHelper = 0;
	_groupConfig->spkCntBufPos = -1;

	_groupConfig->CurrTimeSlice = 0;
	_groupConfig->NewTimeSlice = 0;
	_groupConfig->SliceUpdateTime = 0;
}

// checks whether a connection ID contains plastic synapses O(#connections)
bool SNN::isConnectionPlastic(short int connId) {
	assert(connId != ALL);
	assert(connId < numConnections);
	
	return GET_FIXED_PLASTIC(connectConfigMap[connId].connProp);
}

// FIXME: distinguish the function call at CONFIG_STATE and SETUP_STATE, where groupConfigs[0][] might not be available
// or groupConfigMap is not sync with groupConfigs[0][]
// returns whether group has homeostasis enabled
bool SNN::isGroupWithHomeostasis(int grpId) {
	assert(grpId>=0 && grpId<getNumGroups());
	return (groupConfigMap[grpId].WithHomeostasis);
}

// performs various verification checkups before building the network
void SNN::verifyNetwork() {
	// make sure number of neuron parameters have been accumulated correctly
	// NOTE: this used to be updateParameters
	verifyNumNeurons();

	// make sure STDP post-group has some incoming plastic connections
	verifySTDP();

	// make sure every group with homeostasis also has STDP
	verifyHomeostasis();

	// make sure the max delay is within bound
	assert(maxDelay_ <= MAX_SYN_DELAY);

	// make sure there is sufficient buffer
	if ((networkConfigs[0].maxSpikesD1 + networkConfigs[0].maxSpikesD2) < (numNExcReg + numNInhReg + numNPois) * UNKNOWN_NEURON_MAX_FIRING_RATE) {
		KERNEL_ERROR("Insufficient amount of buffer allocated...");
		exitSimulation(1);
	}

	//make sure the number of pre- and post-connection does not exceed the limitation
	//if (maxNumPostSynGrp > MAX_NUM_POST_SYN) {
	//	for (int g = 0; g < numGroups; g++) {
	//		if (groupConfigMap[g].numPostSynapses>MAX_NUM_POST_SYN)
	//			KERNEL_ERROR("Grp: %s(%d) has too many output synapses (%d), max %d.",groupInfo[g].Name.c_str(),g,
	//						groupConfigMap[g].numPostSynapses,MAX_NUM_POST_SYN);
	//	}
	//	assert(maxNumPostSynGrp <= MAX_NUM_POST_SYN);
	//}

	//if (maxNumPreSynGrp > MAX_NUM_PRE_SYN) {
	//	for (int g = 0; g < numGroups; g++) {
	//		if (groupConfigMap[g].numPreSynapses>MAX_NUM_PRE_SYN)
	//			KERNEL_ERROR("Grp: %s(%d) has too many input synapses (%d), max %d.",groupInfo[g].Name.c_str(),g,
 //							groupConfigMap[g].numPreSynapses,MAX_NUM_PRE_SYN);
	//	}
	//	assert(maxNumPreSynGrp <= MAX_NUM_PRE_SYN);
	//}

	// make sure maxDelay == 1 if STP is enableed
	// \FIXME: need to figure out STP buffer for delays > 1
	if (sim_with_stp && maxDelay_>1) {
		KERNEL_ERROR("STP with delays > 1 ms is currently not supported.");
		exitSimulation(1);
	}

	if (maxDelay_ > MAX_SYN_DELAY) {
		KERNEL_ERROR("You are using a synaptic delay (%d) greater than MAX_SYN_DELAY defined in config.h", maxDelay_);
		exitSimulation(1);
	}
}

// checks whether STDP is set on a post-group with incoming plastic connections
void SNN::verifySTDP() {
	for (int grpId=0; grpId<getNumGroups(); grpId++) {
		if (groupConfigMap[grpId].WithSTDP) {
			// for each post-group, check if any of the incoming connections are plastic
			bool isAnyPlastic = false;
			for (std::map<int, ConnectConfig>::iterator it = connectConfigMap.begin(); it != connectConfigMap.end(); it++) {
				if (it->second.grpDest == grpId) {
					// get syn wt type from connection property
					isAnyPlastic |= GET_FIXED_PLASTIC(it->second.connProp);
					if (isAnyPlastic) {
						// at least one plastic connection found: break while
						break;
					}
				}
			}
			if (!isAnyPlastic) {
				KERNEL_ERROR("If STDP on group %d (%s) is set, group must have some incoming plastic connections.",
					grpId, groupInfo[grpId].Name.c_str());
				exitSimulation(1);
			}
		}
	}
}

// checks whether every group with Homeostasis also has STDP
void SNN::verifyHomeostasis() {
	for (int grpId=0; grpId<getNumGroups(); grpId++) {
		if (groupConfigMap[grpId].WithHomeostasis) {
			if (!groupConfigMap[grpId].WithSTDP) {
				KERNEL_ERROR("If homeostasis is enabled on group %d (%s), then STDP must be enabled, too.",
					grpId, groupInfo[grpId].Name.c_str());
				exitSimulation(1);
			}
		}
	}
}

// checks whether the numN* class members are consistent and complete
void SNN::verifyNumNeurons() {
	int nExcPois = 0;
	int nInhPois = 0;
	int nExcReg = 0;
	int nInhReg = 0;

	//  scan all the groups and find the required information
	//  about the group (numN, numPostSynapses, numPreSynapses and others).
	for(int g=0; g<numGroups; g++)  {
		if (groupConfigMap[g].Type==UNKNOWN_NEURON) {
			KERNEL_ERROR("Unknown group for %d (%s)", g, groupInfo[g].Name.c_str());
			exitSimulation(1);
		}

		if (IS_INHIBITORY_TYPE(groupConfigMap[g].Type) && !(groupConfigMap[g].Type & POISSON_NEURON))
			nInhReg += groupConfigMap[g].SizeN;
		else if (IS_EXCITATORY_TYPE(groupConfigMap[g].Type) && !(groupConfigMap[g].Type & POISSON_NEURON))
			nExcReg += groupConfigMap[g].SizeN;
		else if (IS_EXCITATORY_TYPE(groupConfigMap[g].Type) &&  (groupConfigMap[g].Type & POISSON_NEURON))
			nExcPois += groupConfigMap[g].SizeN;
		else if (IS_INHIBITORY_TYPE(groupConfigMap[g].Type) &&  (groupConfigMap[g].Type & POISSON_NEURON))
			nInhPois += groupConfigMap[g].SizeN;
	}

	// check the newly gathered information with class members
	if (numN != nExcReg+nInhReg+nExcPois+nInhPois) {
		KERNEL_ERROR("nExcReg+nInhReg+nExcPois+nInhPois=%d does not add up to numN=%d",
			nExcReg+nInhReg+nExcPois+nInhPois, numN);
		exitSimulation(1);
	}
	if (numNReg != nExcReg+nInhReg) {
		KERNEL_ERROR("nExcReg+nInhReg=%d does not add up to numNReg=%d", nExcReg+nInhReg, numNReg);
		exitSimulation(1);
	}
	if (numNPois != nExcPois+nInhPois) {
		KERNEL_ERROR("nExcPois+nInhPois=%d does not add up to numNPois=%d", nExcPois+nInhPois, numNPois);
		exitSimulation(1);
	}

	//printf("numN=%d == %d\n",numN,nExcReg+nInhReg+nExcPois+nInhPois);
	//printf("numNReg=%d == %d\n",numNReg, nExcReg+nInhReg);
	//printf("numNPois=%d == %d\n",numNPois, nExcPois+nInhPois);
	
	assert(numN <= 1000000);
	assert((numN > 0) && (numN == numNExcReg + numNInhReg + numNPois));
}

// \FIXME: not sure where this should go... maybe create some helper file?
bool SNN::isPoint3DinRF(const RadiusRF& radius, const Point3D& pre, const Point3D& post) {
	// Note: RadiusRF rad is assumed to be the fanning in to the post neuron. So if the radius is 10 pixels, it means
	// that if you look at the post neuron, it will receive input from neurons that code for locations no more than
	// 10 pixels away. (The opposite is called a response/stimulus field.)

	double rfDist = getRFDist3D(radius, pre, post);
	return (rfDist >= 0.0 && rfDist <= 1.0);
}

double SNN::getRFDist3D(const RadiusRF& radius, const Point3D& pre, const Point3D& post) {
	// Note: RadiusRF rad is assumed to be the fanning in to the post neuron. So if the radius is 10 pixels, it means
	// that if you look at the post neuron, it will receive input from neurons that code for locations no more than
	// 10 pixels away.

	// ready output argument
	// SNN::isPoint3DinRF() will return true (connected) if rfDist e[0.0, 1.0]
	double rfDist = -1.0;

	// pre and post are connected in a generic 3D ellipsoid RF if x^2/a^2 + y^2/b^2 + z^2/c^2 <= 1.0, where
	// x = pre.x-post.x, y = pre.y-post.y, z = pre.z-post.z
	// x < 0 means:  connect if y and z satisfy some constraints, but ignore x
	// x == 0 means: connect if y and z satisfy some constraints, and enforce pre.x == post.x
	if (radius.radX==0 && pre.x!=post.x || radius.radY==0 && pre.y!=post.y || radius.radZ==0 && pre.z!=post.z) {
		rfDist = -1.0;
	} else {
		// 3D ellipsoid: x^2/a^2 + y^2/b^2 + z^2/c^2 <= 1.0
		double xTerm = (radius.radX<=0) ? 0.0 : pow(pre.x-post.x,2)/pow(radius.radX,2);
		double yTerm = (radius.radY<=0) ? 0.0 : pow(pre.y-post.y,2)/pow(radius.radY,2);
		double zTerm = (radius.radZ<=0) ? 0.0 : pow(pre.z-post.z,2)/pow(radius.radZ,2);
		rfDist = xTerm + yTerm + zTerm;
	}

	return rfDist;
}

// will be used in generateSpikesFromRate
// The time between each pair of consecutive events has an exponential distribution with parameter \lambda and
// each of these ISI values is assumed to be independent of other ISI values.
// What follows a Poisson distribution is the actual number of spikes sent during a certain interval.
int SNN::poissonSpike(int currTime, float frate, int refractPeriod) {
	// refractory period must be 1 or greater, 0 means could have multiple spikes specified at the same time.
	assert(refractPeriod>0);
	assert(frate>=0.0f);

	bool done = false;
	int nextTime = 0;
	while (!done) {
		// A Poisson process will always generate inter-spike-interval (ISI) values from an exponential distribution.
		float randVal = drand48();
		int tmpVal  = -log(randVal)/frate;

		// add new ISI to current time
		// this might be faster than keeping currTime fixed until drand48() returns a large enough value for the ISI
		nextTime = currTime + tmpVal;

		// reject new firing time if ISI is smaller than refractory period
		if ((nextTime - currTime) >= refractPeriod)
			done = true;
	}

	assert(nextTime != 0);
	return nextTime;
}

void SNN::partitionSNN() {
	int numAssignedNeurons[MAX_NET_PER_SNN] = {0};

	// partition algorithm, use naive partition for now
	// put excitatory groups to GPU 0 and inhibitory groups to GPU 1
	// this parse separates groups into each local network and assign each group a netId
	for (std::map<int, GroupConfigRT>::iterator it = groupConfigMap.begin(); it != groupConfigMap.end(); it++) {
		//if (IS_EXCITATORY_TYPE(it->second.Type)) {
		//	it->second.netId = 0;
		//	numAssignedNeurons[0] += it->second.SizeN;
		//	groupPartitionLists[0].push_back(it->second);
		//} else if (IS_INHIBITORY_TYPE(it->second.Type)) {
		//	it->second.netId = 1;
		//	numAssignedNeurons[1] += it->second.SizeN;
		//	groupPartitionLists[1].push_back(it->second);
		//} else {
		//	KERNEL_ERROR("Can't assign the group [%d] to any partition", it->second.grpId);
		//	exitSimulation(-1);
		//}

		it->second.netId = 0;
		numAssignedNeurons[0] += it->second.SizeN;
		groupPartitionLists[0].push_back(it->second);
	}

	// this parse finds local connections (i.e., connection configs that conect local groups)
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		if (!groupPartitionLists[netId].empty()) {
			for (std::map<int, ConnectConfig>::iterator connIt = connectConfigMap.begin(); connIt != connectConfigMap.end(); connIt++) {
				if (groupConfigMap[connIt->second.grpSrc].netId == netId && groupConfigMap[connIt->second.grpDest].netId == netId) {
					localConnectLists[netId].push_back(connectConfigMap[connIt->second.connId]);
				}
			}
		}
	}

	// this parse finds external groups and external connections
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		if (!groupPartitionLists[netId].empty()) {
			for (std::map<int, ConnectConfig>::iterator connIt = connectConfigMap.begin(); connIt != connectConfigMap.end(); connIt++) {
				if (groupConfigMap[connIt->second.grpSrc].netId == netId && groupConfigMap[connIt->second.grpDest].netId != netId) {
					numAssignedNeurons[netId] += groupConfigMap[connIt->second.grpDest].SizeN;
					groupPartitionLists[netId].push_back(groupConfigMap[connIt->second.grpDest]);
					externalConnectLists[netId].push_back(connectConfigMap[connIt->second.connId]);
				}
			}
		}
	}

	// assign local neuron ids and, group ids for each local network in the order
	// MPORTANT : NEURON ORGANIZATION/ARRANGEMENT MAP
	// <--- Excitatory --> | <-------- Inhibitory REGION ----------> | <-- Excitatory --> | <-- External -->
	// Excitatory-Regular  | Inhibitory-Regular | Inhibitory-Poisson | Excitatory-Poisson | External Neurons
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		if (!groupPartitionLists[netId].empty()) {
			int availableNeuronId = 0;
			int localGroupId = 0;
			for (int order = 0; order < 5; order++) {
				for (std::list<GroupConfigRT>::iterator grpIt = groupPartitionLists[netId].begin(); grpIt != groupPartitionLists[netId].end(); grpIt++) {
					if (IS_EXCITATORY_TYPE(grpIt->Type) && (grpIt->Type & POISSON_NEURON) && order == 3 && grpIt->netId == netId) {
						availableNeuronId = assignGroup(grpIt, localGroupId, availableNeuronId);
						localGroupId++;
					} else if (IS_INHIBITORY_TYPE(grpIt->Type) && (grpIt->Type & POISSON_NEURON) && order == 2 && grpIt->netId == netId) {
						availableNeuronId = assignGroup(grpIt, localGroupId, availableNeuronId);
						localGroupId++;
					} else if (IS_EXCITATORY_TYPE(grpIt->Type) && !(grpIt->Type & POISSON_NEURON) && order == 0 && grpIt->netId == netId) {
						availableNeuronId = assignGroup(grpIt, localGroupId, availableNeuronId);
						localGroupId++;
					} else if (IS_INHIBITORY_TYPE(grpIt->Type) && !(grpIt->Type & POISSON_NEURON) && order == 1 && grpIt->netId == netId) {
						availableNeuronId = assignGroup(grpIt, localGroupId, availableNeuronId);
						localGroupId++;
					} else if (order == 4 && grpIt->netId != netId) {
						availableNeuronId = assignGroup(grpIt, localGroupId, availableNeuronId);
						localGroupId++;
					}
				}
			}
			assert(availableNeuronId == numAssignedNeurons[netId]);
			assert(localGroupId == groupPartitionLists[netId].size());
		}
	}


	// generation connections among groups according to group and connect configs
	// update ConnectConfig::numberOfConnections
	// update GroupConfig::numPostSynapses, GroupConfig::numPreSynapses
	connectNetwork();

	// print group and connection overview
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		if (!groupPartitionLists[netId].empty()) {
			KERNEL_INFO("\n+ Local Network (%d)", netId);
			KERNEL_INFO("|-+ Group List:");
			for (std::list<GroupConfigRT>::iterator grpIt = groupPartitionLists[netId].begin(); grpIt != groupPartitionLists[netId].end(); grpIt++)
				printGroupInfo(netId, grpIt);
		}

		if (!localConnectLists[netId].empty() || !externalConnectLists[netId].empty()) {
			KERNEL_INFO("|-+ Connection List:");
			for (std::list<ConnectConfig>::iterator connIt = localConnectLists[netId].begin(); connIt != localConnectLists[netId].end(); connIt++)
				printConnectionInfo(netId, connIt);

			for (std::list<ConnectConfig>::iterator connIt = externalConnectLists[netId].begin(); connIt != externalConnectLists[netId].end(); connIt++)
				printConnectionInfo(netId, connIt);
		}
	}

	snnState = PARTITIONED_SNN;
}

int SNN::loadSimulation_internal(bool onlyPlastic) {
	//// TSC: so that we can restore the file position later...
	//// MB: not sure why though...
	//long file_position = ftell(loadSimFID);
	//
	//int tmpInt;
	//float tmpFloat;

	//bool readErr = false; // keep track of reading errors
	//size_t result;


	//// ------- read header ----------------

	//fseek(loadSimFID, 0, SEEK_SET);

	//// read file signature
	//result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//readErr |= (result!=1);
	//if (tmpInt != 294338571) {
	//	KERNEL_ERROR("loadSimulation: Unknown file signature. This does not seem to be a "
	//		"simulation file created with CARLsim::saveSimulation.");
	//	exitSimulation(-1);
	//}

	//// read file version number
	//result = fread(&tmpFloat, sizeof(float), 1, loadSimFID);
	//readErr |= (result!=1);
	//if (tmpFloat > 0.2f) {
	//	KERNEL_ERROR("loadSimulation: Unsupported version number (%f)",tmpFloat);
	//	exitSimulation(-1);
	//}

	//// read simulation time
	//result = fread(&tmpFloat, sizeof(float), 1, loadSimFID);
	//readErr |= (result!=1);

	//// read execution time
	//result = fread(&tmpFloat, sizeof(float), 1, loadSimFID);
	//readErr |= (result!=1);

	//// read number of neurons
	//result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//readErr |= (result!=1);
	//if (tmpInt != numN) {
	//	KERNEL_ERROR("loadSimulation: Number of neurons in file (%d) and simulation (%d) don't match.",
	//		tmpInt, numN);
	//	exitSimulation(-1);
	//}

	//// read number of pre-synapses
	//result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//readErr |= (result!=1);
	//if (numPreSynNet != tmpInt) {
	//	KERNEL_ERROR("loadSimulation: numPreSynNet in file (%d) and simulation (%d) don't match.",
	//		tmpInt, numPreSynNet);
	//	exitSimulation(-1);
	//}

	//// read number of post-synapses
	//result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//readErr |= (result!=1);
	//if (numPostSynNet != tmpInt) {
	//	KERNEL_ERROR("loadSimulation: numPostSynNet in file (%d) and simulation (%d) don't match.",
	//		tmpInt, numPostSynNet);
	//	exitSimulation(-1);
	//}

	//// read number of groups
	//result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//readErr |= (result!=1);
	//if (tmpInt != numGroups) {
	//	KERNEL_ERROR("loadSimulation: Number of groups in file (%d) and simulation (%d) don't match.",
	//		tmpInt, numGroups);
	//	exitSimulation(-1);
	//}

	//// throw reading error instead of proceeding
	//if (readErr) {
	//	fprintf(stderr,"loadSimulation: Error while reading file header");
	//	exitSimulation(-1);
	//}


	//// ------- read group information ----------------

	//for (int g=0; g<numGroups; g++) {
	//	// read StartN
	//	result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//	readErr |= (result!=1);
	//	if (tmpInt != groupConfigs[0][g].StartN) {
	//		KERNEL_ERROR("loadSimulation: StartN in file (%d) and grpInfo (%d) for group %d don't match.",
	//			tmpInt, groupConfigs[0][g].StartN, g);
	//		exitSimulation(-1);
	//	}

	//	// read EndN
	//	result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//	readErr |= (result!=1);
	//	if (tmpInt != groupConfigs[0][g].EndN) {
	//		KERNEL_ERROR("loadSimulation: EndN in file (%d) and grpInfo (%d) for group %d don't match.",
	//			tmpInt, groupConfigs[0][g].EndN, g);
	//		exitSimulation(-1);
	//	}

	//	// read SizeX
	//	result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//	readErr |= (result!=1);

	//	// read SizeY
	//	result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//	readErr |= (result!=1);

	//	// read SizeZ
	//	result = fread(&tmpInt, sizeof(int), 1, loadSimFID);
	//	readErr |= (result!=1);

	//	// read group name
	//	char name[100];
	//	result = fread(name, sizeof(char), 100, loadSimFID);
	//	readErr |= (result!=100);
	//	if (strcmp(name,groupInfo[g].Name.c_str()) != 0) {
	//		KERNEL_ERROR("loadSimulation: Group names in file (%s) and grpInfo (%s) don't match.", name,
	//			groupInfo[g].Name.c_str());
	//		exitSimulation(-1);
	//	}
	//}

	//if (readErr) {
	//	KERNEL_ERROR("loadSimulation: Error while reading group info");
	//	exitSimulation(-1);
	//}


	//// ------- read synapse information ----------------

	//for (int i = 0; i < numN; i++) {
	//	int nrSynapses = 0;

	//	// read number of synapses
	//	result = fread(&nrSynapses, sizeof(int), 1, loadSimFID);
	//	readErr |= (result!=1);

	//	for (int j=0; j<nrSynapses; j++) {
	//		int nIDpre;
	//		int nIDpost;
	//		float weight, maxWeight;
	//		uint8_t delay;
	//		uint8_t plastic;
	//		short int connId;

	//		// read nIDpre
	//		result = fread(&nIDpre, sizeof(int), 1, loadSimFID);
	//		readErr |= (result!=1);
	//		if (nIDpre != i) {
	//			KERNEL_ERROR("loadSimulation: nIDpre in file (%u) and simulation (%u) don't match.", nIDpre, i);
	//			exitSimulation(-1);
	//		}

	//		// read nIDpost
	//		result = fread(&nIDpost, sizeof(int), 1, loadSimFID);
	//		readErr |= (result!=1);
	//		if (nIDpost >= numN) {
	//			KERNEL_ERROR("loadSimulation: nIDpre in file (%u) is larger than in simulation (%u).", nIDpost, numN);
	//			exitSimulation(-1);
	//		}

	//		// read weight
	//		result = fread(&weight, sizeof(float), 1, loadSimFID);
	//		readErr |= (result!=1);

	//		short int gIDpre = managerRuntimeData.grpIds[nIDpre];
	//		if (IS_INHIBITORY_TYPE(groupConfigs[0][gIDpre].Type) && (weight>0)
	//				|| !IS_INHIBITORY_TYPE(groupConfigs[0][gIDpre].Type) && (weight<0)) {
	//			KERNEL_ERROR("loadSimulation: Sign of weight value (%s) does not match neuron type (%s)",
	//				((weight>=0.0f)?"plus":"minus"), 
	//				(IS_INHIBITORY_TYPE(groupConfigs[0][gIDpre].Type)?"inhibitory":"excitatory"));
	//			exitSimulation(-1);
	//		}

	//		// read max weight
	//		result = fread(&maxWeight, sizeof(float), 1, loadSimFID);
	//		readErr |= (result!=1);
	//		if (IS_INHIBITORY_TYPE(groupConfigs[0][gIDpre].Type) && (maxWeight>=0)
	//				|| !IS_INHIBITORY_TYPE(groupConfigs[0][gIDpre].Type) && (maxWeight<=0)) {
	//			KERNEL_ERROR("loadSimulation: Sign of maxWeight value (%s) does not match neuron type (%s)",
	//				((maxWeight>=0.0f)?"plus":"minus"), 
	//				(IS_INHIBITORY_TYPE(groupConfigs[0][gIDpre].Type)?"inhibitory":"excitatory"));
	//			exitSimulation(-1);
	//		}

	//		// read delay
	//		result = fread(&delay, sizeof(uint8_t), 1, loadSimFID);
	//		readErr |= (result!=1);
	//		if (delay > MAX_SYN_DELAY) {
	//			KERNEL_ERROR("loadSimulation: delay in file (%d) is larger than MAX_SYN_DELAY (%d)",
	//				(int)delay, (int)MAX_SYN_DELAY);
	//			exitSimulation(-1);
	//		}

	//		assert(!isnan(weight));
	//		// read plastic/fixed
	//		result = fread(&plastic, sizeof(uint8_t), 1, loadSimFID);
	//		readErr |= (result!=1);

	//		// read connection ID
	//		result = fread(&connId, sizeof(short int), 1, loadSimFID);
	//		readErr |= (result!=1);

	//		if ((plastic && onlyPlastic) || (!plastic && !onlyPlastic)) {
	//			int gIDpost = managerRuntimeData.grpIds[nIDpost];
	//			int connProp = SET_FIXED_PLASTIC(plastic?SYN_PLASTIC:SYN_FIXED);

	//			//setConnection(gIDpre, gIDpost, nIDpre, nIDpost, weight, maxWeight, delay, connProp, connId);
	//			groupInfo[gIDpre].sumPostConn++;
	//			groupInfo[gIDpost].sumPreConn++;

	//			if (delay > groupConfigs[0][gIDpre].MaxDelay)
	//				groupConfigs[0][gIDpre].MaxDelay = delay;
	//		}
	//	}
	//}

	//fseek(loadSimFID,file_position,SEEK_SET);

	return 0;
}

void SNN::generateRuntimeSNN() {
	KERNEL_INFO("");
	KERNEL_INFO("*****************      Initializing %s Simulation      *************************",
		simMode_==GPU_MODE?"GPU":"CPU");

	// 1. genearte configurations for the simulation
	// generate local network configs and accquire maximum size of rumtime data
	generateNetworkConfigs();
	// generate (copy) group configs from groupPartitionLists[]
	generateGroupConfigs();
	// generate (copy) connection configs from localConnectLists[] and exeternalConnectLists[]
	generateConnectConfigs();

	// 2. allocate space of runtime data used by the manager
	// - allocate firingTableD1, firingTableD2, timeTableD1, timeTableD2
	// - reset firingTableD1, firingTableD2, timeTableD1, timeTableD2
	allocateSpikeTables();
	// - allocate voltage, recovery, Izh_a, Izh_b, Izh_c, Izh_d, current, extCurrent, gAMPA, gNMDA, gGABAa, gGABAb
	// lastSpikeTime, nSpikeCnt, stpu, stpx, Npre, Npre_plastic, Npost, cumulativePost, cumulativePre,
	// postSynapticIds, postDelayInfo, wt, wtChange, synSpikeTime, maxSynWt, preSynapticIds, grpIds, connIdsPreIdx,
	// grpDA, grp5HT, grpACh, grpNE, grpDABuffer, grp5HTBuffer, grpAChBuffer, grpNEBuffer, mulSynFast, mulSynSlow
	// - reset all above
	allocateRuntimeData();

	// 3. initialize manager runtime data according to partitions (i.e., local networks)
	// 4a. allocate appropriate memory space (e.g., main memory (CPU) or device memory (GPU)).
	// 4b. load (copy) them to appropriate memory space for execution
	for (int netId = 0; netId < MAX_NET_PER_SNN; netId++) {
		if (!groupPartitionLists[netId].empty()) {	
			// build the runtime data according to local network, group, connection configuirations
			
			// FIXME: local global id conversion
			// generate runtime data for each group
			for(int lGrpId = 0; lGrpId < networkConfigs[netId].numGroups; lGrpId++) {
				if (groupConfigs[netId][lGrpId].Type & POISSON_NEURON) {
					// - init lstSpikeTime
					// - reset avgFiring, stpu, stpx
					// - init stpx
					generatePoissonGroupRuntime(lGrpId);
				} else {
					// - init grpDA, grp5HT, grpACh, grpNE
					// - init Izh_a, Izh_b, Izh_c, Izh_d, voltage, recovery, stpu, stpx
					// - init baseFiring, avgFiring
					// - init lastSpikeTime
					generateGroupRuntime(lGrpId);
				}
			}

			// FIXME: local global id conversion
			// - init grpIds
			for (int nId = 0; nId < networkConfigs[netId].numN; nId++) {
				managerRuntimeData.grpIds[nId] = -1;
				for(int lGrpId = 0; lGrpId < networkConfigs[netId].numGroups; lGrpId++) {
					if (nId >= groupConfigs[netId][lGrpId].localStartN && nId <= groupConfigs[netId][lGrpId].localEndN) {
						managerRuntimeData.grpIds[nId] = (short int)lGrpId;
						break;
					}
				}
				assert(managerRuntimeData.grpIds[nId] != -1);
			}

			// - init mulSynFast, mulSynSlow
			// - init Npre, Npre_plastic, Npost, cumulativePre, cumulativePost, preSynapticIds, postSynapticIds, postDelayInfo
			// - init wt, maxSynWt
			generateConnectionRuntime();

			// - reset current
			resetCurrent();
			// - reset conductance
			resetConductances();
			// - init stpx
			if (networkConfigs[netId].sim_with_stp) {
				for (int i = 0; i < managerRTDSize.maxNumN * (maxDelay_+1); i++)
					managerRuntimeData.stpx[i] = 1.0f; // but memset doesn't work for 1.0
			}

			// - reset wtChange
			// - init synSpikeTime
			// - init wt, maxSynWt
			resetSynapticConnections(false);

			// - allocate spikeGenBits
			// - init GroupConfig.Noffset, NgenFunc
			updateSpikeGeneratorsInit();

			// reset all spike cnt
			// - reset nSpikeCnt
			resetSpikeCnt(ALL);

			allocateSNN();

			// Print the statistics again but dump the results to a file
			//printMemoryInfo(fpDeb_);
		}
	}

	// 5. declare the spiking neural network is excutable
	snnState = EXECUTABLE_SNN;
}

void SNN::resetConductances() {
	if (sim_with_conductances) {
		memset(managerRuntimeData.gAMPA, 0, sizeof(float)*numNReg);
		if (sim_with_NMDA_rise) {
			memset(managerRuntimeData.gNMDA_r, 0, sizeof(float)*numNReg);
			memset(managerRuntimeData.gNMDA_d, 0, sizeof(float)*numNReg);
		} else {
			memset(managerRuntimeData.gNMDA, 0, sizeof(float)*numNReg);
		}
		memset(managerRuntimeData.gGABAa, 0, sizeof(float)*numNReg);
		if (sim_with_GABAb_rise) {
			memset(managerRuntimeData.gGABAb_r, 0, sizeof(float)*numNReg);
			memset(managerRuntimeData.gGABAb_d, 0, sizeof(float)*numNReg);
		} else {
			memset(managerRuntimeData.gGABAb, 0, sizeof(float)*numNReg);
		}
	}
}

void SNN::resetCPUTiming() {
	prevCpuExecutionTime = cumExecutionTime;
	cpuExecutionTime     = 0.0;
}

void SNN::resetCurrent() {
	assert(managerRuntimeData.current != NULL);
	memset(managerRuntimeData.current, 0, sizeof(float) * numNReg);
}

void SNN::resetFiringInformation() {
	// Reset firing tables and time tables to default values..

	// reset Various Times..
	spikeCount	  = 0;
	spikeCountSec = 0;
	spikeCountD2 = 0;
	spikeCountD1 = 0;
	spikeCountD1Sec  = 0;
	spikeCountD2Sec  = 0;

	// reset various times...
	simTimeMs  = 0;
	simTimeSec = 0;
	simTime    = 0;

	// reset the propogation Buffer.
	resetPropogationBuffer();
	// reset Timing  Table..
	resetTimeTable();
}

void SNN::resetGPUTiming() {
	prevGpuExecutionTime = cumExecutionTime;
	gpuExecutionTime     = 0.0;
}

void SNN::resetGroups() {
	for(int g=0; (g < numGroups); g++) {
		// reset spike generator group...
		if (groupConfigs[0][g].isSpikeGenerator) {
			groupConfigs[0][g].CurrTimeSlice = groupConfigs[0][g].NewTimeSlice;
			groupConfigs[0][g].SliceUpdateTime  = 0;
			for(int nid=groupConfigs[0][g].StartN; nid <= groupConfigs[0][g].EndN; nid++)
				resetPoissonNeuron(nid, g);
		}
		// reset regular neuron group...
		else {
			for(int nid=groupConfigs[0][g].StartN; nid <= groupConfigs[0][g].EndN; nid++)
				resetNeuron(nid, g);
		}
	}

	// reset the conductances...
	resetConductances();
}

void SNN::resetNeuromodulator(int grpId) {
	managerRuntimeData.grpDA[grpId] = groupConfigs[0][grpId].baseDP;
	managerRuntimeData.grp5HT[grpId] = groupConfigs[0][grpId].base5HT;
	managerRuntimeData.grpACh[grpId] = groupConfigs[0][grpId].baseACh;
	managerRuntimeData.grpNE[grpId] = groupConfigs[0][grpId].baseNE;
}

void SNN::resetNeuron(int neurId, int grpId) {
	assert(neurId < numNReg);
    if (groupInfo[grpId].Izh_a == -1) {
		KERNEL_ERROR("setNeuronParameters must be called for group %s (%d)",groupInfo[grpId].Name.c_str(),grpId);
		exitSimulation(1);
	}

	managerRuntimeData.Izh_a[neurId] = groupInfo[grpId].Izh_a + groupInfo[grpId].Izh_a_sd*(float)drand48();
	managerRuntimeData.Izh_b[neurId] = groupInfo[grpId].Izh_b + groupInfo[grpId].Izh_b_sd*(float)drand48();
	managerRuntimeData.Izh_c[neurId] = groupInfo[grpId].Izh_c + groupInfo[grpId].Izh_c_sd*(float)drand48();
	managerRuntimeData.Izh_d[neurId] = groupInfo[grpId].Izh_d + groupInfo[grpId].Izh_d_sd*(float)drand48();

	managerRuntimeData.voltage[neurId] = managerRuntimeData.Izh_c[neurId];	// initial values for new_v
	managerRuntimeData.recovery[neurId] = managerRuntimeData.Izh_b[neurId]*managerRuntimeData.voltage[neurId]; // initial values for u


 	if (groupConfigs[0][grpId].WithHomeostasis) {
		// set the baseFiring with some standard deviation.
		if(drand48()>0.5)   {
			managerRuntimeData.baseFiring[neurId] = groupInfo[grpId].baseFiring + groupInfo[grpId].baseFiringSD*-log(drand48());
		} else  {
			managerRuntimeData.baseFiring[neurId] = groupInfo[grpId].baseFiring - groupInfo[grpId].baseFiringSD*-log(drand48());
			if(managerRuntimeData.baseFiring[neurId] < 0.1) managerRuntimeData.baseFiring[neurId] = 0.1;
		}

		if( groupInfo[grpId].baseFiring != 0.0) {
			managerRuntimeData.avgFiring[neurId]  = managerRuntimeData.baseFiring[neurId];
		} else {
			managerRuntimeData.baseFiring[neurId] = 0.0;
			managerRuntimeData.avgFiring[neurId]  = 0;
		}
	}

	managerRuntimeData.lastSpikeTime[neurId]  = MAX_SIMULATION_TIME;

	if(groupConfigs[0][grpId].WithSTP) {
		for (int j=0; j<=maxDelay_; j++) { // is of size maxDelay_+1
			int ind = STP_BUF_POS(neurId,j);
			managerRuntimeData.stpu[ind] = 0.0f;
			managerRuntimeData.stpx[ind] = 1.0f;
		}
	}
}

void SNN::resetMonitors(bool deallocate) {
	// order is important! monitor objects might point to SNN or CARLsim,
	// need to deallocate them first


	// -------------- DEALLOCATE MONITOR OBJECTS ---------------------- //

	// delete all SpikeMonitor objects
	// don't kill SpikeMonitorCore objects, they will get killed automatically
	for (int i=0; i<numSpikeMonitor; i++) {
		if (spikeMonList[i]!=NULL && deallocate) delete spikeMonList[i];
		spikeMonList[i]=NULL;
	}

	// delete all GroupMonitor objects
	// don't kill GroupMonitorCore objects, they will get killed automatically
	for (int i=0; i<numGroupMonitor; i++) {
		if (groupMonList[i]!=NULL && deallocate) delete groupMonList[i];
		groupMonList[i]=NULL;
	}

	// delete all ConnectionMonitor objects
	// don't kill ConnectionMonitorCore objects, they will get killed automatically
	for (int i=0; i<numConnectionMonitor; i++) {
		if (connMonList[i]!=NULL && deallocate) delete connMonList[i];
		connMonList[i]=NULL;
	}
}

void SNN::resetGroupConfigs(bool deallocate) {
	// clear all existing group configurations
	if (deallocate) groupConfigMap.clear();
}

void SNN::resetConnectionConfigs(bool deallocate) {
	// clear all existing connection configurations
	if (deallocate) connectConfigMap.clear();
}

void SNN::resetRuntimeData(bool deallocate) {
	// delete all Spike Counters
	for (int i=0; i<numSpkCnt; i++) {
		if (spkCntBuf[i]!=NULL && deallocate)
			delete[] spkCntBuf[i];
		spkCntBuf[i]=NULL;
	}

	if (pbuf!=NULL && deallocate) delete pbuf;
	if (managerRuntimeData.spikeGenBits!=NULL && deallocate) delete[] managerRuntimeData.spikeGenBits;
	pbuf=NULL; managerRuntimeData.spikeGenBits=NULL;

	// clear data (i.e., concentration of neuromodulator) of groups
	if (managerRuntimeData.grpDA != NULL && deallocate) delete [] managerRuntimeData.grpDA;
	if (managerRuntimeData.grp5HT != NULL && deallocate) delete [] managerRuntimeData.grp5HT;
	if (managerRuntimeData.grpACh != NULL && deallocate) delete [] managerRuntimeData.grpACh;
	if (managerRuntimeData.grpNE != NULL && deallocate) delete [] managerRuntimeData.grpNE;
	managerRuntimeData.grpDA = NULL;
	managerRuntimeData.grp5HT = NULL;
	managerRuntimeData.grpACh = NULL;
	managerRuntimeData.grpNE = NULL;

	// clear assistive data buffer for group monitor
	if (deallocate) {
		for (int i = 0; i < managerRTDSize.maxNumGroups; i++) {
			if (managerRuntimeData.grpDABuffer[i] != NULL) delete [] managerRuntimeData.grpDABuffer[i];
			if (managerRuntimeData.grp5HTBuffer[i] != NULL) delete [] managerRuntimeData.grp5HTBuffer[i];
			if (managerRuntimeData.grpAChBuffer[i] != NULL) delete [] managerRuntimeData.grpAChBuffer[i];
			if (managerRuntimeData.grpNEBuffer[i] != NULL) delete [] managerRuntimeData.grpNEBuffer[i];
			managerRuntimeData.grpDABuffer[i] = NULL;
			managerRuntimeData.grp5HTBuffer[i] = NULL;
			managerRuntimeData.grpAChBuffer[i] = NULL;
			managerRuntimeData.grpNEBuffer[i] = NULL;
		}
	} else {
		memset(managerRuntimeData.grpDABuffer, 0, sizeof(float*) * MAX_GRP_PER_SNN);
		memset(managerRuntimeData.grp5HTBuffer, 0, sizeof(float*) * MAX_GRP_PER_SNN);
		memset(managerRuntimeData.grpAChBuffer, 0, sizeof(float*) * MAX_GRP_PER_SNN);
		memset(managerRuntimeData.grpNEBuffer, 0, sizeof(float*) * MAX_GRP_PER_SNN);
	}


	// -------------- DEALLOCATE CORE OBJECTS ---------------------- //

	if (managerRuntimeData.voltage!=NULL && deallocate) delete[] managerRuntimeData.voltage;
	if (managerRuntimeData.recovery!=NULL && deallocate) delete[] managerRuntimeData.recovery;
	if (managerRuntimeData.current!=NULL && deallocate) delete[] managerRuntimeData.current;
	if (managerRuntimeData.extCurrent!=NULL && deallocate) delete[] managerRuntimeData.extCurrent;
	managerRuntimeData.voltage=NULL; managerRuntimeData.recovery=NULL; managerRuntimeData.current=NULL; managerRuntimeData.extCurrent=NULL;

	if (managerRuntimeData.Izh_a!=NULL && deallocate) delete[] managerRuntimeData.Izh_a;
	if (managerRuntimeData.Izh_b!=NULL && deallocate) delete[] managerRuntimeData.Izh_b;
	if (managerRuntimeData.Izh_c!=NULL && deallocate) delete[] managerRuntimeData.Izh_c;
	if (managerRuntimeData.Izh_d!=NULL && deallocate) delete[] managerRuntimeData.Izh_d;
	managerRuntimeData.Izh_a=NULL; managerRuntimeData.Izh_b=NULL; managerRuntimeData.Izh_c=NULL; managerRuntimeData.Izh_d=NULL;

	if (managerRuntimeData.Npre!=NULL && deallocate) delete[] managerRuntimeData.Npre;
	if (managerRuntimeData.Npre_plastic!=NULL && deallocate) delete[] managerRuntimeData.Npre_plastic;
	if (managerRuntimeData.Npost!=NULL && deallocate) delete[] managerRuntimeData.Npost;
	managerRuntimeData.Npre=NULL; managerRuntimeData.Npre_plastic=NULL; managerRuntimeData.Npost=NULL;

	if (managerRuntimeData.cumulativePre!=NULL && deallocate) delete[] managerRuntimeData.cumulativePre;
	if (managerRuntimeData.cumulativePost!=NULL && deallocate) delete[] managerRuntimeData.cumulativePost;
	managerRuntimeData.cumulativePre=NULL; managerRuntimeData.cumulativePost=NULL;

	if (managerRuntimeData.gAMPA!=NULL && deallocate) delete[] managerRuntimeData.gAMPA;
	if (managerRuntimeData.gNMDA!=NULL && deallocate) delete[] managerRuntimeData.gNMDA;
	if (managerRuntimeData.gNMDA_r!=NULL && deallocate) delete[] managerRuntimeData.gNMDA_r;
	if (managerRuntimeData.gNMDA_d!=NULL && deallocate) delete[] managerRuntimeData.gNMDA_d;
	if (managerRuntimeData.gGABAa!=NULL && deallocate) delete[] managerRuntimeData.gGABAa;
	if (managerRuntimeData.gGABAb!=NULL && deallocate) delete[] managerRuntimeData.gGABAb;
	if (managerRuntimeData.gGABAb_r!=NULL && deallocate) delete[] managerRuntimeData.gGABAb_r;
	if (managerRuntimeData.gGABAb_d!=NULL && deallocate) delete[] managerRuntimeData.gGABAb_d;
	managerRuntimeData.gAMPA=NULL; managerRuntimeData.gNMDA=NULL; managerRuntimeData.gNMDA_r=NULL; managerRuntimeData.gNMDA_d=NULL;
	managerRuntimeData.gGABAa=NULL; managerRuntimeData.gGABAb=NULL; managerRuntimeData.gGABAb_r=NULL; managerRuntimeData.gGABAb_d=NULL;

	if (managerRuntimeData.stpu!=NULL && deallocate) delete[] managerRuntimeData.stpu;
	if (managerRuntimeData.stpx!=NULL && deallocate) delete[] managerRuntimeData.stpx;
	managerRuntimeData.stpu=NULL; managerRuntimeData.stpx=NULL;

	if (managerRuntimeData.avgFiring!=NULL && deallocate) delete[] managerRuntimeData.avgFiring;
	if (managerRuntimeData.baseFiring!=NULL && deallocate) delete[] managerRuntimeData.baseFiring;
	managerRuntimeData.avgFiring=NULL; managerRuntimeData.baseFiring=NULL;

	if (managerRuntimeData.lastSpikeTime!=NULL && deallocate) delete[] managerRuntimeData.lastSpikeTime;
	if (managerRuntimeData.synSpikeTime !=NULL && deallocate) delete[] managerRuntimeData.synSpikeTime;
	if (managerRuntimeData.nSpikeCnt!=NULL && deallocate) delete[] managerRuntimeData.nSpikeCnt;
	managerRuntimeData.lastSpikeTime=NULL; managerRuntimeData.synSpikeTime=NULL; managerRuntimeData.nSpikeCnt=NULL;

	if (managerRuntimeData.postDelayInfo!=NULL && deallocate) delete[] managerRuntimeData.postDelayInfo;
	if (managerRuntimeData.preSynapticIds!=NULL && deallocate) delete[] managerRuntimeData.preSynapticIds;
	if (managerRuntimeData.postSynapticIds!=NULL && deallocate) delete[] managerRuntimeData.postSynapticIds;
	managerRuntimeData.postDelayInfo=NULL; managerRuntimeData.preSynapticIds=NULL; managerRuntimeData.postSynapticIds=NULL;

	if (managerRuntimeData.wt!=NULL && deallocate) delete[] managerRuntimeData.wt;
	if (managerRuntimeData.maxSynWt!=NULL && deallocate) delete[] managerRuntimeData.maxSynWt;
	if (managerRuntimeData.wtChange !=NULL && deallocate) delete[] managerRuntimeData.wtChange;
	managerRuntimeData.wt=NULL; managerRuntimeData.maxSynWt=NULL; managerRuntimeData.wtChange=NULL;

	if (mulSynFast!=NULL && deallocate) delete[] mulSynFast;
	if (mulSynSlow!=NULL && deallocate) delete[] mulSynSlow;
	if (managerRuntimeData.connIdsPreIdx!=NULL && deallocate) delete[] managerRuntimeData.connIdsPreIdx;
	mulSynFast=NULL; mulSynSlow=NULL; managerRuntimeData.connIdsPreIdx=NULL;

	if (managerRuntimeData.grpIds!=NULL && deallocate) delete[] managerRuntimeData.grpIds;
	managerRuntimeData.grpIds=NULL;

	if (managerRuntimeData.firingTableD2!=NULL && deallocate) delete[] managerRuntimeData.firingTableD2;
	if (managerRuntimeData.firingTableD1!=NULL && deallocate) delete[] managerRuntimeData.firingTableD1;
	//if (timeTableD2!=NULL && deallocate) delete[] timeTableD2;
	//if (timeTableD1!=NULL && deallocate) delete[] timeTableD1;
	managerRuntimeData.firingTableD2=NULL; managerRuntimeData.firingTableD1=NULL; //timeTableD2=NULL; timeTableD1=NULL;

	// clear poisson generator
	if (gpuPoissonRand != NULL) delete gpuPoissonRand;
	gpuPoissonRand = NULL;
}


void SNN::resetPoissonNeuron(int nid, int grpId) {
	assert(nid < numN);
	managerRuntimeData.lastSpikeTime[nid]  = MAX_SIMULATION_TIME;
	if (groupConfigs[0][grpId].WithHomeostasis)
		managerRuntimeData.avgFiring[nid] = 0.0;

	if(groupConfigs[0][grpId].WithSTP) {
		for (int j=0; j<=maxDelay_; j++) { // is of size maxDelay_+1
			int ind = STP_BUF_POS(nid,j);
			managerRuntimeData.stpu[nid] = 0.0f;
			managerRuntimeData.stpx[nid] = 1.0f;
		}
	}
}

void SNN::resetPropogationBuffer() {
	pbuf->reset(0, 1023);
}

// resets nSpikeCnt[]
// used for CPU mode
void SNN::resetSpikeCnt(int grpId) {
	int startGrp, endGrp;

	if (grpId == -1) {
		startGrp = 0;
		endGrp = numGroups;
	} else {
		 startGrp = grpId;
		 endGrp = grpId;
	}

	for (int g = startGrp; g<endGrp; g++) {
		int startN = groupConfigs[0][g].StartN;
		int endN   = groupConfigs[0][g].EndN;
		for (int i=startN; i<=endN; i++)
			managerRuntimeData.nSpikeCnt[i] = 0;
	}
}

//Reset wt, wtChange, pre-firing time values to default values, rewritten to
//integrate changes between JMN and MDR -- KDC
//if changeWeights is false, we should keep the values of the weights as they currently
//are but we should be able to change them to plastic or fixed synapses. -- KDC
void SNN::resetSynapticConnections(bool changeWeights) {
	int j;
	// Reset wt,wtChange,pre-firingtime values to default values...
	for(int destGrp=0; destGrp < numGroups; destGrp++) {
		const char* updateStr = (groupConfigs[0][destGrp].newUpdates == true)?"(**)":"";
		KERNEL_DEBUG("Grp: %d:%s s=%d e=%d %s", destGrp, groupInfo[destGrp].Name.c_str(), groupConfigs[0][destGrp].StartN,
					groupConfigs[0][destGrp].EndN,  updateStr);
		KERNEL_DEBUG("Grp: %d:%s s=%d e=%d  %s",  destGrp, groupInfo[destGrp].Name.c_str(), groupConfigs[0][destGrp].StartN,
					groupConfigs[0][destGrp].EndN, updateStr);

		for(int nid=groupConfigs[0][destGrp].StartN; nid <= groupConfigs[0][destGrp].EndN; nid++) {
			unsigned int offset = managerRuntimeData.cumulativePre[nid];
			for (j=0;j<managerRuntimeData.Npre[nid]; j++) {
				managerRuntimeData.wtChange[offset+j] = 0.0;						// synaptic derivatives is reset
				managerRuntimeData.synSpikeTime[offset+j] = MAX_SIMULATION_TIME;	// some large negative value..
			}
			SynInfo *preIdPtr = &(managerRuntimeData.preSynapticIds[managerRuntimeData.cumulativePre[nid]]);
			float* synWtPtr       = &(managerRuntimeData.wt[managerRuntimeData.cumulativePre[nid]]);
			float* maxWtPtr       = &(managerRuntimeData.maxSynWt[managerRuntimeData.cumulativePre[nid]]);
			int prevPreGrp  = -1;

			for (j=0; j < managerRuntimeData.Npre[nid]; j++,preIdPtr++, synWtPtr++, maxWtPtr++) {
				int preId    = GET_CONN_NEURON_ID((*preIdPtr));
				assert(preId < numN);
				int srcGrp = managerRuntimeData.grpIds[preId];
				short int connId = getConnectId(srcGrp, destGrp);
				assert(connId != -1);
				bool   synWtType = GET_FIXED_PLASTIC(connectConfigMap[connId].connProp);
				// print debug information...
				if( prevPreGrp != srcGrp) {
					if(nid==groupConfigs[0][destGrp].StartN) {
						const char* updateStr = (connectConfigMap[connId].newUpdates==true)? "(**)":"";
						KERNEL_DEBUG("\t%d (%s) start=%d, type=%s maxWts = %f %s", srcGrp,
										groupInfo[srcGrp].Name.c_str(), j, (j<managerRuntimeData.Npre_plastic[nid]?"P":"F"),
										connectConfigMap[connId].maxWt, updateStr);
					}
					prevPreGrp = srcGrp;
				}

				if(!changeWeights)
					continue;

				// if connection was plastic or if the connection weights were updated we need to reset the weights
				// TODO: How to account for user-defined connection reset
				if ((synWtType == SYN_PLASTIC) || connectConfigMap[connId].newUpdates) {
					*synWtPtr = generateWeight(connectConfigMap[connId].connProp, connectConfigMap[connId].initWt, connectConfigMap[connId].maxWt, nid, srcGrp);
					*maxWtPtr = connectConfigMap[connId].maxWt;
				}
			}
		}
		groupConfigs[0][destGrp].newUpdates = false;
	}

	// clear all existing connection info...
	for (std::map<int, ConnectConfig>::iterator it = connectConfigMap.begin(); it != connectConfigMap.end(); it++) {
		it->second.newUpdates = false;
	}
}

void SNN::resetTimeTable() {
	memset(timeTableD2, 0, sizeof(int) * (1000 + maxDelay_ + 1));
	memset(timeTableD1, 0, sizeof(int) * (1000 + maxDelay_ + 1));
}

void SNN::resetFiringTable() {
	memset(managerRuntimeData.firingTableD2, 0, sizeof(int) * managerRTDSize.maxMaxSpikeD2);
	memset(managerRuntimeData.firingTableD1, 0, sizeof(int) * managerRTDSize.maxMaxSpikeD1);
}


//! nid=neuron id, sid=synapse id, grpId=group id.
inline SynInfo SNN::SET_CONN_ID(int nId, int sId, int grpId) {
	if (sId > SYNAPSE_ID_MASK) {
		KERNEL_ERROR("Error: Syn Id (%d) exceeds maximum limit (%d) for neuron %d (group %d)", sId, SYNAPSE_ID_MASK, nId, grpId);
		exitSimulation(1);
	}

	if (grpId > GROUP_ID_MASK) {
		KERNEL_ERROR("Error: Group Id (%d) exceeds maximum limit (%d)", grpId, GROUP_ID_MASK);
		exitSimulation(1);
	}

	SynInfo synInfo;
	//p.postId = (((sid)<<CONN_SYN_NEURON_BITS)+((nid)&CONN_SYN_NEURON_MASK));
	//p.grpId  = grpId;
	synInfo.gsId = ((grpId << NUM_SYNAPSE_BITS) | sId);
	synInfo.nId = nId;

	return synInfo;
}

void SNN::setGrpTimeSlice(int grpId, int timeSlice) {
	if (grpId == ALL) {
		for(int g=0; (g < numGroups); g++) {
			if (groupConfigs[0][g].isSpikeGenerator)
				setGrpTimeSlice(g, timeSlice);
		}
	} else {
		assert((timeSlice > 0 ) && (timeSlice <  PROPAGATED_BUFFER_SIZE));
		// the group should be poisson spike generator group
		groupConfigs[0][grpId].NewTimeSlice = timeSlice;
		groupConfigs[0][grpId].CurrTimeSlice = timeSlice;
	}
}

// method to set const member randSeed_
int SNN::setRandSeed(int seed) {
	if (seed<0)
		return time(NULL);
	else if(seed==0)
		return 123;
	else
		return seed;
}

void SNN::startCPUTiming() { prevCpuExecutionTime = cumExecutionTime; }
void SNN::startGPUTiming() { prevGpuExecutionTime = cumExecutionTime; }
void SNN::stopCPUTiming() {
	cpuExecutionTime += (cumExecutionTime - prevCpuExecutionTime);
	prevCpuExecutionTime = cumExecutionTime;
}
void SNN::stopGPUTiming() {
	gpuExecutionTime += (cumExecutionTime - prevGpuExecutionTime);
	prevGpuExecutionTime = cumExecutionTime;
}

// enters testing phase
// in testing, no weight changes can be made, allowing you to evaluate learned weights, etc.
void SNN::startTesting(bool shallUpdateWeights) {
	// because this can be called at any point in time, if we're off the 1-second grid, we want to make
	// sure to apply the accumulated weight changes to the weight matrix
	// but we don't reset the wt update interval counter
	if (shallUpdateWeights && !sim_in_testing) {
		// careful: need to temporarily adjust stdpScaleFactor to make this right
		if (wtANDwtChangeUpdateIntervalCnt_) {
			float storeScaleSTDP = stdpScaleFactor_;
			stdpScaleFactor_ = 1.0f/wtANDwtChangeUpdateIntervalCnt_;

			if (simMode_ == CPU_MODE) {
				updateWeights();
			} else{
				updateWeights_GPU();
			}
			stdpScaleFactor_ = storeScaleSTDP;
		}
	}

	sim_in_testing = true;
	networkConfigs[0].sim_in_testing = true;

	if (simMode_ == GPU_MODE) {
		// copy new network info struct to GPU (|TODO copy only a single boolean)
		copyNetworkConfig();
	}
}

// exits testing phase
void SNN::stopTesting() {
	sim_in_testing = false;
	networkConfigs[0].sim_in_testing = false;

	if (simMode_ == GPU_MODE) {
		// copy new network_info struct to GPU (|TODO copy only a single boolean)
		copyNetworkConfig();
	}
}

void SNN::updateConnectionMonitor(short int connId) {
	for (int monId=0; monId<numConnectionMonitor; monId++) {
		if (connId==ALL || connMonCoreList[monId]->getConnectId()==connId) {
			int timeInterval = connMonCoreList[monId]->getUpdateTimeIntervalSec();
			if (timeInterval==1 || timeInterval>1 && (getSimTime()%timeInterval)==0) {
				// this ConnectionMonitor wants periodic recording
				connMonCoreList[monId]->writeConnectFileSnapshot(simTime,
					getWeightMatrix2D(connMonCoreList[monId]->getConnectId()));
			}
		}
	}
}


std::vector< std::vector<float> > SNN::getWeightMatrix2D(short int connId) {
	assert(connId!=ALL);
	std::vector< std::vector<float> > wtConnId;

	int grpIdPre = connectConfigMap[connId].grpSrc;
	int grpIdPost = connectConfigMap[connId].grpDest;

	// init weight matrix with right dimensions
	for (int i=0; i<groupConfigs[0][grpIdPre].SizeN; i++) {
		std::vector<float> wtSlice;
		for (int j=0; j<groupConfigs[0][grpIdPost].SizeN; j++) {
			wtSlice.push_back(NAN);
		}
		wtConnId.push_back(wtSlice);
	}

	// copy the weights for a given post-group from device
	// \TODO: check if the weights for this grpIdPost have already been copied
	// \TODO: even better, but tricky because of ordering, make copyWeightState connection-based
	if (simMode_==GPU_MODE) {
		copyWeightState(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false, grpIdPost);
	}

	for (int postId=groupConfigs[0][grpIdPost].StartN; postId<=groupConfigs[0][grpIdPost].EndN; postId++) {
		unsigned int pos_ij = managerRuntimeData.cumulativePre[postId];
		for (int i=0; i<managerRuntimeData.Npre[postId]; i++, pos_ij++) {
			// skip synapses that belong to a different connection ID
			if (managerRuntimeData.connIdsPreIdx[pos_ij] != connId) //connInfo->connId)
				continue;

			// find pre-neuron ID and update ConnectionMonitor container
			int preId = GET_CONN_NEURON_ID(managerRuntimeData.preSynapticIds[pos_ij]);
			wtConnId[preId-getGroupStartNeuronId(grpIdPre)][postId-getGroupStartNeuronId(grpIdPost)] =
				fabs(managerRuntimeData.wt[pos_ij]);
		}
	}

	return wtConnId;
}

void SNN::updateGroupMonitor(int grpId) {
	// don't continue if no group monitors in the network
	if (!numGroupMonitor)
		return;

	if (grpId == ALL) {
		for (int g = 0; g < numGroups; g++)
			updateGroupMonitor(g);
	} else {
		// update group monitor of a specific group

		// find index in group monitor arrays
		int monitorId = groupConfigs[0][grpId].GroupMonitorId;

		// don't continue if no group monitor enabled for this group
		if (monitorId < 0)
			return;

		// find last update time for this group
		GroupMonitorCore* grpMonObj = groupMonCoreList[monitorId];
		int lastUpdate = grpMonObj->getLastUpdated();

		// don't continue if time interval is zero (nothing to update)
		if (getSimTime() - lastUpdate <=0)
			return;

		if (getSimTime() - lastUpdate > 1000)
			KERNEL_ERROR("updateGroupMonitor(grpId=%d) must be called at least once every second",grpId);

		if (simMode_ == GPU_MODE) {
			// copy the group status (neuromodulators) from the GPU to the CPU..
			copyGroupState(&managerRuntimeData, &gpuRuntimeData[0], cudaMemcpyDeviceToHost, false);
		}

		// find the time interval in which to update group status
		// usually, we call updateGroupMonitor once every second, so the time interval is [0,1000)
		// however, updateGroupMonitor can be called at any time t \in [0,1000)... so we can have the cases
		// [0,t), [t,1000), and even [t1, t2)
		int numMsMin = lastUpdate%1000; // lower bound is given by last time we called update
		int numMsMax = getSimTimeMs(); // upper bound is given by current time
		if (numMsMax == 0)
			numMsMax = 1000; // special case: full second
		assert(numMsMin < numMsMax);

		// current time is last completed second in milliseconds (plus t to be added below)
		// special case is after each completed second where !getSimTimeMs(): here we look 1s back
		int currentTimeSec = getSimTimeSec();
		if (!getSimTimeMs())
			currentTimeSec--;

		// save current time as last update time
		grpMonObj->setLastUpdated(getSimTime());

		// prepare fast access
		FILE* grpFileId = groupMonCoreList[monitorId]->getGroupFileId();
		bool writeGroupToFile = grpFileId != NULL;
		bool writeGroupToArray = grpMonObj->isRecording();
		float data;

		// Read one peice of data at a time from the buffer and put the data to an appopriate monitor buffer. Later the user
		// may need need to dump these group status data to an output file
		for(int t = numMsMin; t < numMsMax; t++) {
			// fetch group status data, support dopamine concentration currently
			data = managerRuntimeData.grpDABuffer[grpId][t];

			// current time is last completed second plus whatever is leftover in t
			int time = currentTimeSec*1000 + t;

			if (writeGroupToFile) {
				// TODO: write to group status file
			}

			if (writeGroupToArray) {
				grpMonObj->pushData(time, data);
			}
		}

		if (grpFileId!=NULL) // flush group status file
			fflush(grpFileId);
	}
}

void SNN::updateSpikesFromGrp(int grpId) {
	assert(groupConfigs[0][grpId].isSpikeGenerator==true);

	bool done;
	//static FILE* _fp = fopen("spikes.txt", "w");
	int currTime = simTime;

	int timeSlice = groupConfigs[0][grpId].CurrTimeSlice;
	groupConfigs[0][grpId].SliceUpdateTime  = simTime;

	// we dont generate any poisson spike if during the
	// current call we might exceed the maximum 32 bit integer value
	if ((currTime + timeSlice) == MAX_SIMULATION_TIME || (currTime + timeSlice) < 0)
		return;

	if (groupConfigs[0][grpId].spikeGen) {
		generateSpikesFromFuncPtr(grpId);
	} else {
		// current mode is GPU, and GPU would take care of poisson generators
		// and other information about refractor period etc. So no need to continue further...
#if !TESTING_CPU_GPU_POISSON
    if(simMode_ == GPU_MODE)
      return;
#endif

		generateSpikesFromRate(grpId);
	}
}

void SNN::updateSpikeGenerators() {
	for(int g=0; g<numGroups; g++) {
		if (groupConfigs[0][g].isSpikeGenerator) {
			// This evaluation is done to check if its time to get new set of spikes..
			// check whether simTime has advance more than the current time slice, in which case we need to schedule
			// spikes for the next time slice
			// we always have to run this the first millisecond of a new runNetwork call; that is,
			// when simTime==simTimeRunStart
			if(((simTime-groupConfigs[0][g].SliceUpdateTime) >= groupConfigs[0][g].CurrTimeSlice || simTime == simTimeRunStart)) {
				updateSpikesFromGrp(g);
			}
		}
	}
}

void SNN::updateSpikeGeneratorsInit() {
	int cnt=0;
	for(int g=0; (g < numGroups); g++) {
		if (groupConfigs[0][g].isSpikeGenerator) {
			// This is done only during initialization
			groupConfigs[0][g].CurrTimeSlice = groupConfigs[0][g].NewTimeSlice;

			// we only need NgenFunc for spike generator callbacks that need to transfer their spikes to the GPU
			if (groupConfigs[0][g].spikeGen) {
				groupConfigs[0][g].Noffset = NgenFunc;
				NgenFunc += groupConfigs[0][g].SizeN;
			}
			//Note: updateSpikeFromGrp() will be called first time in updateSpikeGenerators()
			//updateSpikesFromGrp(g);
			cnt++;
			assert(cnt <= numSpikeGenGrps);
		}
	}

	// spikeGenBits can be set only once..
	assert(managerRuntimeData.spikeGenBits == NULL);
	assert(networkConfigs[0].numNPois == NgenFunc);

	if (NgenFunc) {
		managerRuntimeData.spikeGenBits = new uint32_t[NgenFunc/32+1];
		// increase the total memory size used by the routine...
		cpuSnnSz.addInfoSize += sizeof(managerRuntimeData.spikeGenBits[0])*(NgenFunc/32+1);
	}
}

/*!
 * \brief Allocate and reset SNN::maxSpikesD1, SNN::maxSpikesD2 and allocate sapce for SNN::firingTableD1 and SNN::firingTableD2
 *
 * \note SpikeTables include firingTableD1(D2) and timeTableD1(D2)
 */
void SNN::allocateSpikeTables() {
	managerRuntimeData.firingTableD2 = new int[managerRTDSize.maxMaxSpikeD2];
	managerRuntimeData.firingTableD1 = new int[managerRTDSize.maxMaxSpikeD1];
	resetFiringTable();
	
	// timeTableD1(D2) are statically allocated
	resetTimeTable();
	cpuSnnSz.spikingInfoSize += sizeof(int) * ((managerRTDSize.maxMaxSpikeD2 + managerRTDSize.maxMaxSpikeD1) + 2 * (1000 + maxDelay_ + 1));
}

// updates simTime, returns true when new second started
bool SNN::updateTime() {
	bool finishedOneSec = false;

	// done one second worth of simulation
	// update relevant parameters...now
	if(++simTimeMs == 1000) {
		simTimeMs = 0;
		simTimeSec++;
		finishedOneSec = true;
	}

	simTime++;
	if(simTime == MAX_SIMULATION_TIME || simTime < 0){
        // reached the maximum limit of the simulation time using 32 bit value...
        KERNEL_WARN("Maximum Simulation Time Reached...Resetting simulation time");
	}

	return finishedOneSec;
}


void SNN::updateSpikeMonitor(int grpId) {
	// don't continue if no spike monitors in the network
	if (!numSpikeMonitor)
		return;

	if (grpId==ALL) {
		for (int g=0; g<numGroups; g++)
			updateSpikeMonitor(g);
	} else {
		// update spike monitor of a specific group

		// find index in spike monitor arrays
		int monitorId = groupConfigs[0][grpId].SpikeMonitorId;

		// don't continue if no spike monitor enabled for this group
		if (monitorId<0)
			return;

		// find last update time for this group
		SpikeMonitorCore* spkMonObj = spikeMonCoreList[monitorId];
		long int lastUpdate = spkMonObj->getLastUpdated();

		// don't continue if time interval is zero (nothing to update)
		if ( ((long int)getSimTime()) - lastUpdate <=0)
			return;

		if ( ((long int)getSimTime()) - lastUpdate > 1000)
			KERNEL_ERROR("updateSpikeMonitor(grpId=%d) must be called at least once every second",grpId);

        // AER buffer max size warning here.
        // Because of C++ short-circuit evaluation, the last condition should not be evaluated
        // if the previous conditions are false.
        if (spkMonObj->getAccumTime() > LONG_SPIKE_MON_DURATION \
                && this->getGroupNumNeurons(grpId) > LARGE_SPIKE_MON_GRP_SIZE \
                && spkMonObj->isBufferBig()){
            // change this warning message to correct message
            KERNEL_WARN("updateSpikeMonitor(grpId=%d) is becoming very large. (>%lu MB)",grpId,(long int) MAX_SPIKE_MON_BUFFER_SIZE/1024 );// make this better
            KERNEL_WARN("Reduce the cumulative recording time (currently %lu minutes) or the group size (currently %d) to avoid this.",spkMonObj->getAccumTime()/(1000*60),this->getGroupNumNeurons(grpId));
       }
		if (simMode_ == GPU_MODE) {
			// copy the neuron firing information from the GPU to the CPU..
			copyFiringInfo_GPU();
		}

		// find the time interval in which to update spikes
		// usually, we call updateSpikeMonitor once every second, so the time interval is [0,1000)
		// however, updateSpikeMonitor can be called at any time t \in [0,1000)... so we can have the cases
		// [0,t), [t,1000), and even [t1, t2)
		int numMsMin = lastUpdate%1000; // lower bound is given by last time we called update
		int numMsMax = getSimTimeMs(); // upper bound is given by current time
		if (numMsMax==0)
			numMsMax = 1000; // special case: full second
		assert(numMsMin<numMsMax);

		// current time is last completed second in milliseconds (plus t to be added below)
		// special case is after each completed second where !getSimTimeMs(): here we look 1s back
		int currentTimeSec = getSimTimeSec();
		if (!getSimTimeMs())
			currentTimeSec--;

		// save current time as last update time
		spkMonObj->setLastUpdated( (long int)getSimTime() );

		// prepare fast access
		FILE* spkFileId = spikeMonCoreList[monitorId]->getSpikeFileId();
		bool writeSpikesToFile = spkFileId!=NULL;
		bool writeSpikesToArray = spkMonObj->getMode()==AER && spkMonObj->isRecording();

		// Read one spike at a time from the buffer and put the spikes to an appopriate monitor buffer. Later the user
		// may need need to dump these spikes to an output file
		for (int k=0; k < 2; k++) {
			unsigned int* timeTablePtr = (k==0)?timeTableD2:timeTableD1;
			int* fireTablePtr = (k==0)?managerRuntimeData.firingTableD2:managerRuntimeData.firingTableD1;
			for(int t=numMsMin; t<numMsMax; t++) {
				for(int i=timeTablePtr[t+maxDelay_]; i<timeTablePtr[t+maxDelay_+1];i++) {
					// retrieve the neuron id
					int nid = fireTablePtr[i];
					//if (simMode_ == GPU_MODE)
					//	nid = GET_FIRING_TABLE_NID(nid);
					assert(nid < numN);

					// make sure neuron belongs to currently relevant group
					int this_grpId = managerRuntimeData.grpIds[nid];
					if (this_grpId != grpId)
						continue;

					// adjust nid to be 0-indexed for each group
					// this way, if a group has 10 neurons, their IDs in the spike file and spike monitor will be
					// indexed from 0..9, no matter what their real nid is
					nid -= groupConfigs[0][grpId].StartN;
					assert(nid>=0);

					// current time is last completed second plus whatever is leftover in t
					int time = currentTimeSec*1000 + t;

					if (writeSpikesToFile) {
						int cnt;
						cnt = fwrite(&time, sizeof(int), 1, spkFileId); assert(cnt==1);
						cnt = fwrite(&nid,  sizeof(int), 1, spkFileId); assert(cnt==1);
					}

					if (writeSpikesToArray) {
						spkMonObj->pushAER(time,nid);
					}
				}
			}
		}

		if (spkFileId!=NULL) // flush spike file
			fflush(spkFileId);
	}
}

//------------------------------ legacy code --------------------------------//

// We parallelly cleanup the postSynapticIds array to minimize any other wastage in that array by compacting the store
// Appropriate alignment specified by ALIGN_COMPACTION macro is used to ensure some level of alignment (if necessary)
//void SNN::compactConnections() {
//	unsigned int* tmp_cumulativePost = new unsigned int[numN];
//	unsigned int* tmp_cumulativePre  = new unsigned int[numN];
//	unsigned int lastCnt_pre         = 0;
//	unsigned int lastCnt_post        = 0;
//
//	tmp_cumulativePost[0]   = 0;
//	tmp_cumulativePre[0]    = 0;
//
//	for(int i=1; i < numN; i++) {
//		lastCnt_post = tmp_cumulativePost[i-1]+managerRuntimeData.Npost[i-1]; //position of last pointer
//		lastCnt_pre  = tmp_cumulativePre[i-1]+managerRuntimeData.Npre[i-1]; //position of last pointer
//		#if COMPACTION_ALIGNMENT_POST
//			lastCnt_post= lastCnt_post + COMPACTION_ALIGNMENT_POST-lastCnt_post%COMPACTION_ALIGNMENT_POST;
//			lastCnt_pre = lastCnt_pre  + COMPACTION_ALIGNMENT_PRE- lastCnt_pre%COMPACTION_ALIGNMENT_PRE;
//		#endif
//		tmp_cumulativePost[i] = lastCnt_post;
//		tmp_cumulativePre[i]  = lastCnt_pre;
//		assert(tmp_cumulativePost[i] <= managerRuntimeData.cumulativePost[i]);
//		assert(tmp_cumulativePre[i]  <= managerRuntimeData.cumulativePre[i]);
//	}
//
//	// compress the post_synaptic array according to the new values of the tmp_cumulative counts....
//	unsigned int tmp_numPostSynNet = tmp_cumulativePost[numN-1]+managerRuntimeData.Npost[numN-1];
//	unsigned int tmp_numPreSynNet  = tmp_cumulativePre[numN-1]+managerRuntimeData.Npre[numN-1];
//	assert(tmp_numPostSynNet <= allocatedPost);
//	assert(tmp_numPreSynNet  <= allocatedPre);
//	assert(tmp_numPostSynNet <= numPostSynNet);
//	assert(tmp_numPreSynNet  <= numPreSynNet);
//	KERNEL_DEBUG("******************");
//	KERNEL_DEBUG("CompactConnection: ");
//	KERNEL_DEBUG("******************");
//	KERNEL_DEBUG("old_postCnt = %d, new_postCnt = %d", numPostSynNet, tmp_numPostSynNet);
//	KERNEL_DEBUG("old_preCnt = %d,  new_postCnt = %d", numPreSynNet,  tmp_numPreSynNet);
//
//	// new buffer with required size + 100 bytes of additional space just to provide limited overflow
//	SynInfo* tmp_postSynapticIds   = new SynInfo[tmp_numPostSynNet+100];
//
//	// new buffer with required size + 100 bytes of additional space just to provide limited overflow
//	SynInfo* tmp_preSynapticIds	= new SynInfo[tmp_numPreSynNet+100];
//	float* tmp_wt	    	  		= new float[tmp_numPreSynNet+100];
//	float* tmp_maxSynWt   	  		= new float[tmp_numPreSynNet+100];
//	short int *tmp_cumConnIdPre 		= new short int[tmp_numPreSynNet+100];
//	float *tmp_mulSynFast 			= new float[numConnections];
//	float *tmp_mulSynSlow  			= new float[numConnections];
//
//	// compact synaptic information
//	for(int i=0; i<numN; i++) {
//		assert(tmp_cumulativePost[i] <= managerRuntimeData.cumulativePost[i]);
//		assert(tmp_cumulativePre[i]  <= managerRuntimeData.cumulativePre[i]);
//		for( int j=0; j<managerRuntimeData.Npost[i]; j++) {
//			unsigned int tmpPos = tmp_cumulativePost[i]+j;
//			unsigned int oldPos = managerRuntimeData.cumulativePost[i]+j;
//			tmp_postSynapticIds[tmpPos] = managerRuntimeData.postSynapticIds[oldPos];
//			tmp_SynapticDelay[tmpPos]   = tmp_SynapticDelay[oldPos];
//		}
//		for( int j=0; j<managerRuntimeData.Npre[i]; j++) {
//			unsigned int tmpPos =  tmp_cumulativePre[i]+j;
//			unsigned int oldPos =  managerRuntimeData.cumulativePre[i]+j;
//			tmp_preSynapticIds[tmpPos]  = managerRuntimeData.preSynapticIds[oldPos];
//			tmp_maxSynWt[tmpPos] 	    = managerRuntimeData.maxSynWt[oldPos];
//			tmp_wt[tmpPos]              = managerRuntimeData.wt[oldPos];
//			tmp_cumConnIdPre[tmpPos]	= managerRuntimeData.connIdsPreIdx[oldPos];
//		}
//	}
//
//	// delete old buffer space
//	delete[] managerRuntimeData.postSynapticIds;
//	managerRuntimeData.postSynapticIds = tmp_postSynapticIds;
//	cpuSnnSz.networkInfoSize -= (sizeof(SynInfo)*numPostSynNet);
//	cpuSnnSz.networkInfoSize += (sizeof(SynInfo)*(tmp_numPostSynNet+100));
//
//	delete[] managerRuntimeData.cumulativePost;
//	managerRuntimeData.cumulativePost  = tmp_cumulativePost;
//
//	delete[] managerRuntimeData.cumulativePre;
//	managerRuntimeData.cumulativePre   = tmp_cumulativePre;
//
//	delete[] managerRuntimeData.maxSynWt;
//	managerRuntimeData.maxSynWt = tmp_maxSynWt;
//	cpuSnnSz.synapticInfoSize -= (sizeof(float)*numPreSynNet);
//	cpuSnnSz.synapticInfoSize += (sizeof(float)*(tmp_numPreSynNet+100));
//
//	delete[] managerRuntimeData.wt;
//	managerRuntimeData.wt = tmp_wt;
//	cpuSnnSz.synapticInfoSize -= (sizeof(float)*numPreSynNet);
//	cpuSnnSz.synapticInfoSize += (sizeof(float)*(tmp_numPreSynNet+100));
//
//	delete[] managerRuntimeData.connIdsPreIdx;
//	managerRuntimeData.connIdsPreIdx = tmp_cumConnIdPre;
//	cpuSnnSz.synapticInfoSize -= (sizeof(short int)*numPreSynNet);
//	cpuSnnSz.synapticInfoSize += (sizeof(short int)*(tmp_numPreSynNet+100));
//
//	// compact connection-centric information
//	for (int i=0; i<numConnections; i++) {
//		tmp_mulSynFast[i] = mulSynFast[i];
//		tmp_mulSynSlow[i] = mulSynSlow[i];
//	}
//	delete[] mulSynFast;
//	delete[] mulSynSlow;
//	mulSynFast = tmp_mulSynFast;
//	mulSynSlow = tmp_mulSynSlow;
//	cpuSnnSz.networkInfoSize -= (2*sizeof(uint8_t)*numPreSynNet);
//	cpuSnnSz.networkInfoSize += (2*sizeof(uint8_t)*(tmp_numPreSynNet+100));
//
//
//	delete[] managerRuntimeData.preSynapticIds;
//	managerRuntimeData.preSynapticIds  = tmp_preSynapticIds;
//	cpuSnnSz.synapticInfoSize -= (sizeof(SynInfo)*numPreSynNet);
//	cpuSnnSz.synapticInfoSize += (sizeof(SynInfo)*(tmp_numPreSynNet+100));
//
//	numPreSynNet	= tmp_numPreSynNet;
//	numPostSynNet	= tmp_numPostSynNet;
//}

//The post synaptic connections are sorted based on delay here so that we can reduce storage requirement
//and generation of spike at the post-synaptic side.
//We also create the delay_info array has the delay_start and delay_length parameter
//void SNN::reorganizeDelay()
//{
//	for(int grpId=0; grpId < numGroups; grpId++) {
//		for(int nid=groupConfigs[0][grpId].StartN; nid <= groupConfigs[0][grpId].EndN; nid++) {
//			unsigned int jPos=0;					// this points to the top of the delay queue
//			unsigned int cumN=managerRuntimeData.cumulativePost[nid];	// cumulativePost[] is unsigned int
//			unsigned int cumDelayStart=0; 			// Npost[] is unsigned short
//			for(int td = 0; td < maxDelay_; td++) {
//				unsigned int j=jPos;				// start searching from top of the queue until the end
//				unsigned int cnt=0;					// store the number of nodes with a delay of td;
//				while(j < managerRuntimeData.Npost[nid]) {
//					// found a node j with delay=td and we put
//					// the delay value = 1 at array location td=0;
//					if(td==(tmp_SynapticDelay[cumN+j]-1)) {
//						assert(jPos<managerRuntimeData.Npost[nid]);
//						swapConnections(nid, j, jPos);
//
//						jPos=jPos+1;
//						cnt=cnt+1;
//					}
//					j=j+1;
//				}
//
//				// update the delay_length and start values...
//				managerRuntimeData.postDelayInfo[nid*(maxDelay_+1)+td].delay_length	     = cnt;
//				managerRuntimeData.postDelayInfo[nid*(maxDelay_+1)+td].delay_index_start  = cumDelayStart;
//				cumDelayStart += cnt;
//
//				assert(cumDelayStart <= managerRuntimeData.Npost[nid]);
//			}
//
//			// total cumulative delay should be equal to number of post-synaptic connections at the end of the loop
//			assert(cumDelayStart == managerRuntimeData.Npost[nid]);
//			for(unsigned int j=1; j < managerRuntimeData.Npost[nid]; j++) {
//				unsigned int cumN=managerRuntimeData.cumulativePost[nid]; // cumulativePost[] is unsigned int
//				if( tmp_SynapticDelay[cumN+j] < tmp_SynapticDelay[cumN+j-1]) {
//	  				KERNEL_ERROR("Post-synaptic delays not sorted correctly... id=%d, delay[%d]=%d, delay[%d]=%d",
//						nid, j, tmp_SynapticDelay[cumN+j], j-1, tmp_SynapticDelay[cumN+j-1]);
//					assert( tmp_SynapticDelay[cumN+j] >= tmp_SynapticDelay[cumN+j-1]);
//				}
//			}
//		}
//	}
//}

//void SNN::swapConnections(int nid, int oldPos, int newPos) {
//	unsigned int cumN=managerRuntimeData.cumulativePost[nid];
//
//	// Put the node oldPos to the top of the delay queue
//	SynInfo tmp = managerRuntimeData.postSynapticIds[cumN+oldPos];
//	managerRuntimeData.postSynapticIds[cumN+oldPos]= managerRuntimeData.postSynapticIds[cumN+newPos];
//	managerRuntimeData.postSynapticIds[cumN+newPos]= tmp;
//
//	// Ensure that you have shifted the delay accordingly....
//	uint8_t tmp_delay = tmp_SynapticDelay[cumN+oldPos];
//	tmp_SynapticDelay[cumN+oldPos] = tmp_SynapticDelay[cumN+newPos];
//	tmp_SynapticDelay[cumN+newPos] = tmp_delay;
//
//	// update the pre-information for the postsynaptic neuron at the position oldPos.
//	SynInfo  postInfo = managerRuntimeData.postSynapticIds[cumN+oldPos];
//	int  post_nid = GET_CONN_NEURON_ID(postInfo);
//	int  post_sid = GET_CONN_SYN_ID(postInfo);
//
//	SynInfo* preId    = &(managerRuntimeData.preSynapticIds[managerRuntimeData.cumulativePre[post_nid]+post_sid]);
//	int  pre_nid  = GET_CONN_NEURON_ID((*preId));
//	int  pre_sid  = GET_CONN_SYN_ID((*preId));
//	int  pre_gid  = GET_CONN_GRP_ID((*preId));
//	assert (pre_nid == nid);
//	assert (pre_sid == newPos);
//	*preId = SET_CONN_ID( pre_nid, oldPos, pre_gid);
//
//	// update the pre-information for the postsynaptic neuron at the position newPos
//	postInfo = managerRuntimeData.postSynapticIds[cumN+newPos];
//	post_nid = GET_CONN_NEURON_ID(postInfo);
//	post_sid = GET_CONN_SYN_ID(postInfo);
//
//	preId    = &(managerRuntimeData.preSynapticIds[managerRuntimeData.cumulativePre[post_nid]+post_sid]);
//	pre_nid  = GET_CONN_NEURON_ID((*preId));
//	pre_sid  = GET_CONN_SYN_ID((*preId));
//	pre_gid  = GET_CONN_GRP_ID((*preId));
//	assert (pre_nid == nid);
//	assert (pre_sid == oldPos);
//	*preId = SET_CONN_ID( pre_nid, newPos, pre_gid);
//}

// set one specific connection from neuron id 'src' to neuron id 'dest'
//inline void SNN::setConnection(int srcGrp,  int destGrp,  unsigned int src, unsigned int dest, float synWt,
//									float maxWt, uint8_t dVal, int connProp, short int connId) {
//	assert(dest<=CONN_SYN_NEURON_MASK);			// total number of neurons is less than 1 million within a GPU
//	assert((dVal >=1) && (dVal <= maxDelay_));
//
//	// adjust sign of weight based on pre-group (negative if pre is inhibitory)
//	synWt = isExcitatoryGroup(srcGrp) ? fabs(synWt) : -1.0*fabs(synWt);
//	maxWt = isExcitatoryGroup(srcGrp) ? fabs(maxWt) : -1.0*fabs(maxWt);
//
//	// we have exceeded the number of possible connection for one neuron
//	if(managerRuntimeData.Npost[src] >= groupConfigs[0][srcGrp].numPostSynapses)	{
//		KERNEL_ERROR("setConnection(%d (Grp=%s), %d (Grp=%s), %f, %d)", src, groupInfo[srcGrp].Name.c_str(),
//					dest, groupInfo[destGrp].Name.c_str(), synWt, dVal);
//		KERNEL_ERROR("Large number of postsynaptic connections established (%d), max for this group %d.", managerRuntimeData.Npost[src], groupConfigs[0][srcGrp].numPostSynapses);
//		exitSimulation(1);
//	}
//
//	if(managerRuntimeData.Npre[dest] >= groupConfigs[0][destGrp].numPreSynapses) {
//		KERNEL_ERROR("setConnection(%d (Grp=%s), %d (Grp=%s), %f, %d)", src, groupInfo[srcGrp].Name.c_str(),
//					dest, groupInfo[destGrp].Name.c_str(), synWt, dVal);
//		KERNEL_ERROR("Large number of presynaptic connections established (%d), max for this group %d.", managerRuntimeData.Npre[dest], groupConfigs[0][destGrp].numPreSynapses);
//		exitSimulation(1);
//	}
//
//	int p = managerRuntimeData.Npost[src];
//
//	assert(managerRuntimeData.Npost[src] >= 0);
//	assert(managerRuntimeData.Npre[dest] >= 0);
//	assert((src * maxNumPostSynGrp + p) / numN < maxNumPostSynGrp); // divide by numN to prevent INT overflow
//
//	unsigned int post_pos = managerRuntimeData.cumulativePost[src] + managerRuntimeData.Npost[src];
//	unsigned int pre_pos  = managerRuntimeData.cumulativePre[dest] + managerRuntimeData.Npre[dest];
//
//	assert(post_pos < numPostSynNet);
//	assert(pre_pos  < numPreSynNet);
//
//	//generate a new postSynapticIds id for the current connection
//	managerRuntimeData.postSynapticIds[post_pos]   = SET_CONN_ID(dest, managerRuntimeData.Npre[dest], destGrp);
//	tmp_SynapticDelay[post_pos] = dVal;
//
//	managerRuntimeData.preSynapticIds[pre_pos] = SET_CONN_ID(src, managerRuntimeData.Npost[src], srcGrp);
//	managerRuntimeData.wt[pre_pos] 	  = synWt;
//	managerRuntimeData.maxSynWt[pre_pos] = maxWt;
//	managerRuntimeData.connIdsPreIdx[pre_pos] = connId;
//
//	bool synWtType = GET_FIXED_PLASTIC(connProp);
//
//	if (synWtType == SYN_PLASTIC) {
//		sim_with_fixedwts = false; // if network has any plastic synapses at all, this will be set to true
//		managerRuntimeData.Npre_plastic[dest]++;
//		// homeostasis
//		if (groupConfigs[0][destGrp].WithHomeostasis && groupConfigs[0][destGrp].homeoId ==-1)
//			groupConfigs[0][destGrp].homeoId = dest; // this neuron info will be printed
//	}
//
//	managerRuntimeData.Npre[dest] += 1;
//	managerRuntimeData.Npost[src] += 1;
//
//	groupInfo[srcGrp].numPostConn++;
//	groupInfo[destGrp].numPreConn++;
//
//	if (managerRuntimeData.Npost[src] > groupInfo[srcGrp].maxPostConn)
//		groupInfo[srcGrp].maxPostConn = managerRuntimeData.Npost[src];
//	if (managerRuntimeData.Npre[dest] > groupInfo[destGrp].maxPreConn)
//	groupInfo[destGrp].maxPreConn = managerRuntimeData.Npre[src];
//}