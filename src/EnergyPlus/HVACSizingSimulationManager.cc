// EnergyPlus, Copyright (c) 1996-2016, The Board of Trustees of the University of Illinois and
// The Regents of the University of California, through Lawrence Berkeley National Laboratory
// (subject to receipt of any required approvals from the U.S. Dept. of Energy). All rights
// reserved.
//
// If you have questions about your rights to use or distribute this software, please contact
// Berkeley Lab's Innovation & Partnerships Office at IPO@lbl.gov.
//
// NOTICE: This Software was developed under funding from the U.S. Department of Energy and the
// U.S. Government consequently retains certain rights. As such, the U.S. Government has been
// granted for itself and others acting on its behalf a paid-up, nonexclusive, irrevocable,
// worldwide license in the Software to reproduce, distribute copies to the public, prepare
// derivative works, and perform publicly and display publicly, and to permit others to do so.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted
// provided that the following conditions are met:
//
// (1) Redistributions of source code must retain the above copyright notice, this list of
//     conditions and the following disclaimer.
//
// (2) Redistributions in binary form must reproduce the above copyright notice, this list of
//     conditions and the following disclaimer in the documentation and/or other materials
//     provided with the distribution.
//
// (3) Neither the name of the University of California, Lawrence Berkeley National Laboratory,
//     the University of Illinois, U.S. Dept. of Energy nor the names of its contributors may be
//     used to endorse or promote products derived from this software without specific prior
//     written permission.
//
// (4) Use of EnergyPlus(TM) Name. If Licensee (i) distributes the software in stand-alone form
//     without changes from the version obtained under this License, or (ii) Licensee makes a
//     reference solely to the software portion of its product, Licensee must refer to the
//     software as "EnergyPlus version X" software, where "X" is the version number Licensee
//     obtained under this License and may not use a different name for the software. Except as
//     specifically required in this Section (4), Licensee shall not use in a company name, a
//     product name, in advertising, publicity, or other promotional activities any name, trade
//     name, trademark, logo, or other designation of "EnergyPlus", "E+", "e+" or confusingly
//     similar designation, without Lawrence Berkeley National Laboratory's prior written consent.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
// IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// You are under no obligation whatsoever to provide any bug fixes, patches, or upgrades to the
// features, functionality or performance of the source code ("Enhancements") to anyone; however,
// if you choose to make your Enhancements available either publicly, or directly to Lawrence
// Berkeley National Laboratory, without imposing a separate written license agreement for such
// Enhancements, then you hereby grant the following license: a non-exclusive, royalty-free
// perpetual license to install, use, modify, prepare derivative works, incorporate into other
// computer software, distribute, and sublicense such enhancements or derivative works thereof,
// in binary and source code form.

// C++ Headers
#include <cassert>
#include <vector>

// ObjexxFCL Headers
#include <ObjexxFCL/gio.hh>

// EnergyPlus Headers
#include <HVACSizingSimulationManager.hh>
#include <DataEnvironment.hh>
#include <DataErrorTracking.hh>
#include <DataGlobals.hh>
#include <DataPlant.hh>
#include <DataReportingFlags.hh>
#include <DataSizing.hh>
#include <DataSystemVariables.hh>
#include <DisplayRoutines.hh>
#include <EMSManager.hh>
#include <ExteriorEnergyUse.hh>
#include <FluidProperties.hh>
#include <General.hh>
#include <HeatBalanceManager.hh>
#include <PlantManager.hh>
#include <PlantPipingSystemsManager.hh>
#include <SimulationManager.hh>
#include <SizingAnalysisObjects.hh>
#include <SQLiteProcedures.hh>
#include <UtilityRoutines.hh>
#include <WeatherManager.hh>

namespace EnergyPlus {

	void HVACSizingSimulationManager::DetermineSizingAnalysesNeeded()
	{

		//currently the only types of advanced sizing analysis available are:
		//		1. coincident plant sizing
		//		2. central air system cooling coil adjustments
		// expect more specialized sizing analysis objects to be added, so minimizing code here and jump to a worker method once we know an instance is to be created.

		// loop over SysSizInput struct and find air systems that are to use advanced sizing

		for ( int i = 1; i <= DataSizing::NumSysSizInput; ++i ) {
			if ( DataSizing::SysSizInput( i ).precoolHumRatWasAutosized ) {
				this->createOrFindNewAirSizingAnalysisObject( i , AirLoopSizingAnalsysis::outdoorAirCoolingCoilLeavingHumRat );
			}
			if ( DataSizing::SysSizInput( i ).coolSupHumRatWasAutosized ) {
				this->createOrFindNewAirSizingAnalysisObject( i , AirLoopSizingAnalsysis::mainCoolingCoilLeavingHumRat );
			}
			if ( DataSizing::SysSizInput( i ).CoolingCapMethod == DataSizing::ConcurrentDeviceAdjustedCoolingCapacity ) {
				this->createOrFindNewAirSizingAnalysisObject( i , AirLoopSizingAnalsysis::mainCoolingCoilInletConditions );
			}
		}


		//Loop over PlantSizData struct and find those plant loops that are to use coincident sizing
		for ( int i = 1; i <= DataSizing::NumPltSizInput; ++i ) {

			if ( DataSizing::PlantSizData( i ).ConcurrenceOption == DataSizing::Coincident ) {

				//create an instance of analysis object for each loop
				this->CreateNewCoincidentPlantAnalysisObject( DataSizing::PlantSizData(i).PlantLoopName, i );

			}
		}



	}

	void HVACSizingSimulationManager::CreateNewCoincidentPlantAnalysisObject(
		std::string const & PlantLoopName,
		int const PlantSizingIndex
	)
	{
		Real64 density;
		Real64 cp;

		//find plant loop number
		for ( int i = 1; i <= DataPlant::TotNumLoops; ++i ) {
			if ( PlantLoopName == DataPlant::PlantLoop( i ).Name ) { //found it

				density = FluidProperties::GetDensityGlycol( DataPlant::PlantLoop( i ).FluidName,
								DataGlobals::InitConvTemp, DataPlant::PlantLoop( i ).FluidIndex,
								"createNewCoincidentPlantAnalysisObject" );
				cp = FluidProperties::GetSpecificHeatGlycol( DataPlant::PlantLoop( i ).FluidName,
								DataGlobals::InitConvTemp, DataPlant::PlantLoop( i ).FluidIndex,
								"createNewCoincidentPlantAnalysisObject" );

				plantCoincAnalyObjs.emplace_back( PlantLoopName, i, DataPlant::PlantLoop( i ).LoopSide( DataPlant::SupplySide ).NodeNumIn, density,
					cp, DataSizing::PlantSizData( PlantSizingIndex ).NumTimeStepsInAvg, PlantSizingIndex );
			}
		}
	}

	void HVACSizingSimulationManager::createOrFindNewAirSizingAnalysisObject(
		int const SystemSizingIndex,
		AirLoopSizingAnalsysis::SizingAnalysisTypeEnum typeOfAnalysis
	) 
	{
		bool foundIt = false;
		// first see if this airloop already has an advanced sizing object created
		for ( auto & a : this->airLoopAdjustAnalyObjs ) {
			if ( a.sysSizingIndex == SystemSizingIndex ) { // found it
				a.typesOfSizingAdjustments.emplace_back( typeOfAnalysis );
				foundIt = true;
				return;
			}
		}		
			// find air loop index
		if ( ! foundIt ) {
			// find coil
			this->airLoopAdjustAnalyObjs.emplace_back( DataSizing::SysSizInput( SystemSizingIndex ).AirPriLoopName, DataSizing::SysSizInput( SystemSizingIndex ).AirLoopNum, SystemSizingIndex, typeOfAnalysis );

		}




	
	}



	void HVACSizingSimulationManager::SetupSizingAnalyses()
	{

		for ( auto & P : plantCoincAnalyObjs ) {
			//call setup log routine for each coincident plant analysis object
			P.supplyInletNodeFlow_LogIndex = sizingLogger.SetupVariableSizingLog(
				DataLoopNode::Node( P.supplySideInletNodeNum ).MassFlowRate,
				P.numTimeStepsInAvg );
			P.supplyInletNodeTemp_LogIndex = sizingLogger.SetupVariableSizingLog(
				DataLoopNode::Node( P.supplySideInletNodeNum ).Temp,
				P.numTimeStepsInAvg );
			if ( DataSizing::PlantSizData(P.plantSizingIndex).LoopType == DataSizing::HeatingLoop
					|| DataSizing::PlantSizData(P.plantSizingIndex).LoopType == DataSizing::SteamLoop ) {
				P.loopDemand_LogIndex = sizingLogger.SetupVariableSizingLog(
					DataPlant::PlantReport(P.plantLoopIndex ).HeatingDemand,
					P.numTimeStepsInAvg );
			} else if ( DataSizing::PlantSizData(P.plantSizingIndex).LoopType == DataSizing::CoolingLoop
						|| DataSizing::PlantSizData(P.plantSizingIndex).LoopType == DataSizing::CondenserLoop ) {
				P.loopDemand_LogIndex = sizingLogger.SetupVariableSizingLog(
					DataPlant::PlantReport(P.plantLoopIndex ).CoolingDemand,
					P.numTimeStepsInAvg );
			}

		}

		for ( auto & a : airLoopAdjustAnalyObjs ) {
	//		if ( std::find( a.typesOfSizingAdjustments.begin(), a.typesOfSizingAdjustments.end(), AirLoopSizingAnalsysis::mainCoolingCoilLeavingHumRat || AirLoopSizingAnalsysis::mainCoolingCoilInletConditions )  != a.typesOfSizingAdjustments.end() ) {
			if ( a.mainCoilLogged ) {
				// set up data logs on main cooling coil inlet node
				a.mainCCoilInEnthalpy_LogIndex = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilInletNodeIndex ).Enthalpy, a.numTimeStepsInAvg);
				a.mainCCoilinHumRat_LogIndex   = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilInletNodeIndex ).HumRat, a.numTimeStepsInAvg);
				a.mainCCoilInMdot_LogIndex     = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilInletNodeIndex ).MassFlowRate, a.numTimeStepsInAvg);
				a.mainCCoilInTempDB_LogIndex   = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilInletNodeIndex ).Temp, a.numTimeStepsInAvg);

				// set up data logs on main cooling coil outlet node
				a.mainCCoilOutEnthalpy_LogIndex = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilOutletNodeIndex ).Enthalpy, a.numTimeStepsInAvg);
				a.mainCCoilOutHumRat_LogIndex   = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilOutletNodeIndex ).HumRat, a.numTimeStepsInAvg);
				a.mainCCoilOutMdot_LogIndex     = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilOutletNodeIndex ).MassFlowRate, a.numTimeStepsInAvg);
				a.mainCCoilOutTempDB_LogIndex   = sizingLogger.SetupVariableSizingLog( DataLoopNode::Node( a.mainCoolingCoilOutletNodeIndex ).Temp, a.numTimeStepsInAvg);

			}
		
		}

	}

	void HVACSizingSimulationManager::PostProcessLogs()
	{
		//this function calls methods on log objects to do general processing on all the logged data in the framework
		for ( auto & L : sizingLogger.logObjs ) {
			L.AverageSysTimeSteps(); // collapse subtimestep data into zone step data
			L.ProcessRunningAverage(); // apply zone step moving average
		}
	}

	void HVACSizingSimulationManager::ProcessCoincidentPlantSizeAdjustments(
		int const HVACSizingIterCount
	)
	{

		//first pass through coincident plant objects to check new sizes and see if more iteration needed
		plantCoinAnalyRequestsAnotherIteration = false;
		for ( auto & P : plantCoincAnalyObjs ) {
			//step 1 find maximum flow rate on concurrent return temp and load
			P.newFoundMassFlowRateTimeStamp = sizingLogger.logObjs[ P.supplyInletNodeFlow_LogIndex ].GetLogVariableDataMax();
			P.peakMdotCoincidentDemand = sizingLogger.logObjs[ P.loopDemand_LogIndex].GetLogVariableDataAtTimestamp ( P.newFoundMassFlowRateTimeStamp );
			P.peakMdotCoincidentReturnTemp = sizingLogger.logObjs[ P.supplyInletNodeTemp_LogIndex].GetLogVariableDataAtTimestamp ( P.newFoundMassFlowRateTimeStamp );

			// step 2 find maximum load and concurrent flow and return temp
			P.NewFoundMaxDemandTimeStamp = sizingLogger.logObjs[ P.loopDemand_LogIndex ].GetLogVariableDataMax();
			P.peakDemandMassFlow = sizingLogger.logObjs[ P.supplyInletNodeFlow_LogIndex ].GetLogVariableDataAtTimestamp( P.NewFoundMaxDemandTimeStamp );
			P.peakDemandReturnTemp = sizingLogger.logObjs[ P.supplyInletNodeTemp_LogIndex ].GetLogVariableDataAtTimestamp( P.NewFoundMaxDemandTimeStamp );

			P.ResolveDesignFlowRate( HVACSizingIterCount );
			if ( P.anotherIterationDesired ) {
				plantCoinAnalyRequestsAnotherIteration = true;
			}
		}

		// as more sizing adjustments are added this will need to change to consider all not just plant coincident
		DataGlobals::FinalSizingHVACSizingSimIteration = plantCoinAnalyRequestsAnotherIteration;

	}

	void HVACSizingSimulationManager::processAirLoopAdjustments( 
		int const HVACSizingIterCount 
	)
	{

		std::vector< AirloopCoilZTimestepProcessor > coilProcVec;

		airLoopSizingAdjustAnalyRequestsAnotherIteration = false;
		for ( auto & a : airLoopAdjustAnalyObjs ) {
			coilProcVec.clear();
			if ( a.mainCoilLogged ) {
		
				std::vector< Real64> inHvec   = sizingLogger.logObjs[ a.mainCCoilInEnthalpy_LogIndex ].getLogVariableVector();

				std::vector< Real64 > outHvec = sizingLogger.logObjs[ a.mainCCoilOutEnthalpy_LogIndex ].getLogVariableVector();

				std::vector< Real64 > mdotVec = sizingLogger.logObjs[ a.mainCCoilInMdot_LogIndex ].getLogVariableVector();

				std::vector< Real64 > outHumRat = sizingLogger.logObjs[ a.mainCCoilOutHumRat_LogIndex ].getLogVariableVector();


				for ( std::size_t i = 0; i < inHvec.size(); ++i ) {

					coilProcVec.emplace_back ( inHvec[i] , outHvec[ i ], mdotVec[ i ], outHumRat[ i ] )  ; 
				}
				int maxCoolingIndex = -1;
				Real64 maxCoolingLoad = 0.0;
				for ( std::size_t i = 0; i < coilProcVec.size(); ++i ) {
					if ( coilProcVec[ i ].qdot < maxCoolingLoad ) {
						maxCoolingLoad = coilProcVec[ i ].qdot;
						maxCoolingIndex = i;
					}
				}

				Real64 newHumRat = coilProcVec[ maxCoolingIndex ].outW;

				a.newFoundMainCoilOutHumRatTimeStamp = sizingLogger.logObjs[ a.mainCCoilOutHumRat_LogIndex ].getLogVariableDataAtIndex( maxCoolingIndex );

				a.resolveMainCoilOutletHumidityRatio( HVACSizingIterCount );



			}




		
		}
	}

	void HVACSizingSimulationManager::RedoKickOffAndResize()
	{
		using DataGlobals::KickOffSimulation;
		using DataGlobals::RedoSizesHVACSimulation;
		using namespace WeatherManager;
		using namespace SimulationManager;
		bool ErrorsFound( false );
		KickOffSimulation = true;
		RedoSizesHVACSimulation = true;

		ResetEnvironmentCounter();
		SetupSimulation( ErrorsFound );

		KickOffSimulation = false;
		RedoSizesHVACSimulation = false;
	}

	void HVACSizingSimulationManager::UpdateSizingLogsZoneStep()
	{
		sizingLogger.UpdateSizingLogValuesZoneStep();
	}

	void HVACSizingSimulationManager::UpdateSizingLogsSystemStep()
	{
		sizingLogger.UpdateSizingLogValuesSystemStep();
	}

	std::unique_ptr< HVACSizingSimulationManager > hvacSizingSimulationManager;

	void ManageHVACSizingSimulation( bool & ErrorsFound )
	{
		using DataEnvironment::EnvironmentName;
		using DataEnvironment::CurMnDy;
		using DataEnvironment::CurrentOverallSimDay;
		using DataEnvironment::TotalOverallSimDays;
		using General::TrimSigDigits;
		using EMSManager::ManageEMS;
		using PlantPipingSystemsManager::SimulateGroundDomains;
		using ExteriorEnergyUse::ManageExteriorEnergyUse;
		using DataSystemVariables::ReportDuringHVACSizingSimulation;
		using DataErrorTracking::ExitDuringSimulations;

		using namespace WeatherManager;
		using namespace DataGlobals;
		using namespace DataReportingFlags;
		using namespace HeatBalanceManager;

		hvacSizingSimulationManager = std::unique_ptr< HVACSizingSimulationManager >( new HVACSizingSimulationManager() );

		bool Available; // an environment is available to process
		int HVACSizingIterCount;
		static gio::Fmt Format_700("('Environment:WarmupDays,',I3)");
		static gio::Fmt fmtLD( "*" );

		hvacSizingSimulationManager->DetermineSizingAnalysesNeeded();

		hvacSizingSimulationManager->SetupSizingAnalyses();

		DisplayString( "Beginning HVAC Sizing Simulation" );
		DoingHVACSizingSimulations = true;
		DoOutputReporting = true;

		ResetEnvironmentCounter();

		// iterations over set of sizing periods for HVAC sizing Simulation, will break out if no more are needed
		for ( HVACSizingIterCount = 1; HVACSizingIterCount <= HVACSizingSimMaxIterations; ++HVACSizingIterCount ) {

			//need to extend Environment structure array to distinguish the HVAC Sizing Simulations from the regular run of that sizing period, repeats for each set
			AddDesignSetToEnvironmentStruct( HVACSizingIterCount );

			WarmupFlag = true;
			Available = true;
			for ( int i = 1; i <= NumOfEnvrn; ++i ) { // loop over environments

				GetNextEnvironment(Available, ErrorsFound);
				if (ErrorsFound) break;
				if (!Available) continue;

				hvacSizingSimulationManager->sizingLogger.SetupSizingLogsNewEnvironment();

			//	if (!DoDesDaySim) continue; // not sure about this, may need to force users to set this on input for this method, but maybe not
				if ( KindOfSim == ksRunPeriodWeather ) continue;
				if ( KindOfSim == ksDesignDay ) continue;
				if ( KindOfSim == ksRunPeriodDesign ) continue;

				if ( Environment(Envrn).HVACSizingIterationNum != HVACSizingIterCount ) continue;

				if ( ReportDuringHVACSizingSimulation ) {
					if ( sqlite ) {
						sqlite->sqliteBegin();
						sqlite->createSQLiteEnvironmentPeriodRecord( DataEnvironment::CurEnvirNum, DataEnvironment::EnvironmentName, DataGlobals::KindOfSim );
						sqlite->sqliteCommit();
					}
				}
				ExitDuringSimulations = true;

				DisplayString("Initializing New Environment Parameters, HVAC Sizing Simulation");

				BeginEnvrnFlag = true;
				EndEnvrnFlag = false;
				//EndMonthFlag = false;
				WarmupFlag = true;
				DayOfSim = 0;
				DayOfSimChr = "0";
				NumOfWarmupDays = 0;

				ManageEMS(emsCallFromBeginNewEvironment); // calling point

				while ( (DayOfSim < NumOfDayInEnvrn) || (WarmupFlag) ) { // Begin day loop ...

					if ( ReportDuringHVACSizingSimulation ) {
						if ( sqlite ) sqlite->sqliteBegin(); // setup for one transaction per day
					}
					++DayOfSim;
					gio::write( DayOfSimChr, fmtLD ) << DayOfSim;
					strip(DayOfSimChr);
					if ( !WarmupFlag ) {
						++CurrentOverallSimDay;
						DisplaySimDaysProgress(CurrentOverallSimDay, TotalOverallSimDays);
					} else {
						DayOfSimChr = "0";
					}
					BeginDayFlag = true;
					EndDayFlag = false;

					if ( WarmupFlag ) {
						++NumOfWarmupDays;
						cWarmupDay = TrimSigDigits(NumOfWarmupDays);
						DisplayString("Warming up {" + cWarmupDay + '}');
					} else if (DayOfSim == 1) {
						DisplayString("Starting HVAC Sizing Simulation at " + CurMnDy + " for " + EnvironmentName);
						gio::write(OutputFileInits, Format_700) << NumOfWarmupDays;
					} else if (DisplayPerfSimulationFlag) {
						DisplayString("Continuing Simulation at " + CurMnDy + " for " + EnvironmentName);
						DisplayPerfSimulationFlag = false;
					}

					for ( HourOfDay = 1; HourOfDay <= 24; ++HourOfDay ) { // Begin hour loop ...

						BeginHourFlag = true;
						EndHourFlag = false;

						for (TimeStep = 1; TimeStep <= NumOfTimeStepInHour; ++TimeStep) {
							if ( AnySlabsInModel || AnyBasementsInModel ) {
								SimulateGroundDomains( false );
							}

							BeginTimeStepFlag = true;


							// Set the End__Flag variables to true if necessary.  Note that
							// each flag builds on the previous level.  EndDayFlag cannot be
							// .TRUE. unless EndHourFlag is also .TRUE., etc.  Note that the
							// EndEnvrnFlag and the EndSimFlag cannot be set during warmup.
							// Note also that BeginTimeStepFlag, EndTimeStepFlag, and the
							// SubTimeStepFlags can/will be set/reset in the HVAC Manager.

							if ( TimeStep == NumOfTimeStepInHour ) {
								EndHourFlag = true;
								if ( HourOfDay == 24 ) {
									EndDayFlag = true;
									if ( !WarmupFlag && (DayOfSim == NumOfDayInEnvrn) ) {
										EndEnvrnFlag = true;
									}
								}
							}

							ManageWeather();

							ManageExteriorEnergyUse();

							ManageHeatBalance();

							BeginHourFlag = false;
							BeginDayFlag = false;
							BeginEnvrnFlag = false;
							BeginSimFlag = false;
							BeginFullSimFlag = false;

						} // TimeStep loop

						PreviousHour = HourOfDay;

					} // ... End hour loop.
					if ( ReportDuringHVACSizingSimulation ) {
						if ( sqlite ) sqlite->sqliteCommit(); // one transaction per day
					}
				} // ... End day loop.


			} // ... End environment loop.


			if ( ErrorsFound ) {
				ShowFatalError( "Error condition occurred.  Previous Severe Errors cause termination." );
			}


			hvacSizingSimulationManager->PostProcessLogs();

			hvacSizingSimulationManager->processAirLoopAdjustments( HVACSizingIterCount );

			hvacSizingSimulationManager->ProcessCoincidentPlantSizeAdjustments( HVACSizingIterCount );

			hvacSizingSimulationManager->RedoKickOffAndResize();

			if ( ! hvacSizingSimulationManager->plantCoinAnalyRequestsAnotherIteration && ! hvacSizingSimulationManager->airLoopSizingAdjustAnalyRequestsAnotherIteration ) {
				// jump out of for loop, or change for to a while
				break;
			}

			hvacSizingSimulationManager->sizingLogger.IncrementSizingPeriodSet();

		} // End HVAC Sizing Iteration loop

		WarmupFlag = false;
		DoOutputReporting = true;
		DoingHVACSizingSimulations = false;
		hvacSizingSimulationManager.reset(); // delete/reset unique_ptr
	}
}
