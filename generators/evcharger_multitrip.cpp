/** $Id: evcharger_multitrip.cpp
	Copyright (C) 2012 Battelle Memorial Institute
	Copyright (C) 2013 Joel Courtney Ausgrid (changes - see SVN Diff)
 **/
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include "evcharger_multitrip.h"

// FROM INVERTER METHODS /////////////////////////////////////
static PASSCONFIG passconfig = PC_BOTTOMUP|PC_POSTTOPDOWN;
static PASSCONFIG clockpass = PC_BOTTOMUP;
////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// evcharger_multitrip CLASS FUNCTIONS
//////////////////////////////////////////////////////////////////////////
CLASS* evcharger_multitrip::oclass = NULL;
CLASS* evcharger_multitrip::pclass = NULL;

// The EV Charger Multitrip
// @param		*module		MODULE
// @return		NULL
evcharger_multitrip::evcharger_multitrip(MODULE *module)
{
	// first time initialisation
	if (oclass==NULL)
	{
		// register the class definition
		oclass = gl_register_class(module,"evcharger_multitrip",sizeof(evcharger_multitrip),PC_PRETOPDOWN|PC_BOTTOMUP|PC_POSTTOPDOWN|PC_AUTOLOCK);
		if (oclass==NULL)
			throw "unable to register class evcharger_multitrip";
		else
			oclass->trl = TRL_PROOF;
			
		// publish the class properties
		// Configuration for GLM file parsing, recorders, interobject interaction
		if (gl_publish_variable(oclass,
			// PT_INHERIT,		"powerflow",
			// PT_char1024,	"name", PADDR(vehicle_name), PT_DESCRIPTION, "Details of the trip atm",
			
			// Location details
			PT_enumeration,	"vehicle_location", PADDR(vehicle_data.location), PT_DESCRIPTION, "Current location of the vehicle",
				PT_KEYWORD,		"UNKNOWN",VL_UNKNOWN,
				PT_KEYWORD,		"HOME",VL_HOME,
				PT_KEYWORD,		"DESTINATION",VL_DESTINATION,
				PT_KEYWORD,		"DRIVING_HOME",VL_DESTINATION_TO_HOME,
				PT_KEYWORD,		"DRIVING_DESTINATION",VL_HOME_TO_DESTINATION,
			PT_bool,		"enabled",	PADDR(enabled), PT_DESCRIPTION, "Flag to enable/disable charge point (for charging and discharging) true:enable charge point; false:disable charge point",
			
			// Battery information
			PT_double,		"charge_rate[W]", PADDR(charge_rate), PT_DESCRIPTION, "Current demanded charge rate of the vehicle",
			PT_double,		"battery_capacity[kWh]",PADDR(vehicle_data.battery_capacity), PT_DESCRIPTION, "Current capacity of the battery in kWh",
			PT_double,		"battery_soc[%]",PADDR(vehicle_data.battery_soc), PT_DESCRIPTION, "State of charge of battery",
			PT_double,		"battery_size[kWh]",PADDR(vehicle_data.battery_size), PT_DESCRIPTION, "Full capacity of battery",
			PT_double,		"mileage_efficiency[mile/kWh]",PADDR(vehicle_data.mileage_efficiency), PT_DESCRIPTION, "Efficiency of drive train in mile/kWh",
			PT_double,		"maximum_charge_rate[W]",PADDR(vehicle_data.max_charge_rate), PT_DESCRIPTION, "Maximum charge rate of the charger in watts",
			PT_double,		"maximum_discharge_rate[W]",PADDR(vehicle_data.max_discharge_rate), PT_DESCRIPTION, "Maximum discharge rate of charger in watts",
			PT_double,		"charging_efficiency[unit]",PADDR(vehicle_data.charge_efficiency), PT_DESCRIPTION, "Efficiency of charger (ratio) when charging",
			
			// Charge Regime in use
			PT_enumeration,	"charge_regime", PADDR(vehicle_data.charge_regime), PT_DESCRIPTION, "Regime in use to charge the vehicle when at home",
				PT_KEYWORD,		"FULL_POWER",FULL_POWER,
				PT_KEYWORD,		"LEAST_POWER",LEAST_POWER,
				PT_KEYWORD,		"REMOTE_CONTROLLED",REMOTE_CONTROLLED,
						
			// Vehicle Trip Files
			PT_char1024,	"vehicle_trips_csv_file", PADDR(vehicle_trips_csv_file), PT_DESCRIPTION, "Path to .CSV file with vehicle trips data",
			PT_char1024,	"vehicle_trips_indexes", PADDR(vehicle_trips_indexes_str), PT_DESCRIPTION, "A single line CSV string of the indexes of trips for this EV",
			
			// Current Status
			PT_char1024,	"current_vehicle_trip", PADDR(current_vehicle_trip_str), PT_DESCRIPTION, "Details of the trip atm",
			PT_int32,		"trip_id",PADDR(trip_id), PT_DESCRIPTION, "ID of current active trip",
			
			// Current Load State
			PT_complex,		"phaseA_V",		PADDR(load_data.phaseA_V),		PT_DESCRIPTION, "Phase A Voltage [V]",
			PT_complex,		"phaseB_V",		PADDR(load_data.phaseB_V),		PT_DESCRIPTION, "Phase B Voltage [V]",
			PT_complex,		"phaseC_V",		PADDR(load_data.phaseC_V),		PT_DESCRIPTION, "Phase C Voltage [V]",
			PT_complex,		"phaseA_I",		PADDR(load_data.phaseA_I),		PT_DESCRIPTION, "Phase A Current [A]",
			PT_complex,		"phaseB_I",		PADDR(load_data.phaseB_I),		PT_DESCRIPTION, "Phase B Current [A]",
			PT_complex,		"phaseC_I",		PADDR(load_data.phaseC_I),		PT_DESCRIPTION, "Phase C Current [A]",
			PT_complex,		"phaseA_S",		PADDR(load_data.phaseA_S),		PT_DESCRIPTION, "Phase A Power [VA]",
			PT_complex,		"phaseB_S",		PADDR(load_data.phaseB_S),		PT_DESCRIPTION, "Phase B Power [VA]",
			PT_complex,		"phaseC_S",		PADDR(load_data.phaseC_S),		PT_DESCRIPTION, "Phase C Power [VA]",

			NULL)<1) 
			GL_THROW("unable to publish properties in %s",__FILE__);
	}
}

// Creates a new evcharger_multitrip instance
// @return		int			?
int evcharger_multitrip::create()
{
	enabled		= true;

	// Is the charge_rate needed? Yes
	charge_rate							= 0.0;		//	Starts off
	// Containers for calculations
	// TODO: pf_out added to the published variables
	pf_out								= 0.99;		//	Defaults to 0.99
	VA_Out								= complex(0.0,0.0);	//	Defaults to 0.
	
	// Car information - battery info is left uninitialized
	vehicle_data.battery_capacity		= -1.0;		//	Flags
	vehicle_data.battery_size			= -1.0;		//	Flags
	vehicle_data.battery_soc			= -1.0;		//	Flags
	vehicle_data.charge_efficiency		= 0.90;		//	Default in-out efficiency of 90%
	vehicle_data.location				= VL_HOME;	//	Default start at home
	vehicle_data.max_charge_rate		= 10000.0;	//	Watts
	vehicle_data.max_discharge_rate		= 5000.0;	//	Watts
	vehicle_data.mileage_efficiency		= 3.846;	//	(0.26 kWh/mile - from MKM report)
	vehicle_data.next_state_change		= 0;		//	Next time the state will change
	
	// Default to no trips
	vehicle_data.vehicle_trip_start		= NULL;
	vehicle_data.vehicle_trip_end   	= NULL;
	vehicle_data.vehicle_trip_current	= NULL;
	
	// Default to full power
	vehicle_data.charge_regime			= FULL_POWER;
	
	// Default power information
	// Current load
	load_data.phaseA_V		= 0;
	load_data.phaseB_V		= 0;
	load_data.phaseC_V		= 0;
	load_data.phaseA_I		= 0;
	load_data.phaseB_I		= 0;
	load_data.phaseC_I		= 0;
	load_data.phaseA_S		= 0;
	load_data.phaseB_S		= 0;
	load_data.phaseC_S		= 0;
	
	// Default to at home thus trip_id is zero
	trip_id								= 0;
	
	// Trip data
	vehicle_trips_csv_file[0]			= '\0';		//	Null file for csv file of USA's National Household Travel Survey - http://nhts.ornl.gov/
	vehicle_trips_indexes_str[0]		= '\0';		//	Null for the vehicle string too
	
	// Set the default internal private variables
	prev_time						= 0;		//	Set the default
	prev_time_postsync				= 0;		//	Set this default too
	off_nominal_time				= false;	//	Assumes minimum timesteps aren't screwing things up, by default

	return 1; /* create returns 1 on success, 0 on failure */
}

// Creates a new evcharger_multitrip instance
// @param		*parent[OBJECT]				Pointer to the parent object(?)
// @return		int							1 - Flag of completion
int evcharger_multitrip::init(OBJECT *parent)
{
	OBJECT		*obj = OBJECTHDR(this);
	extern complex default_line_current[3];
	extern complex default_line_voltage[3];
	
	TIMESTAMP	init_time, temp_time;
	DATETIME	init_date, temp_date;

	VehicleTrip	*temp_vehicle_trip;

	// TODO: Break out parent initialisation
	// construct circuit variable map to meter
	static		complex default_line123_voltage[3], default_line1_current[3];
	static int	default_meter_status = 1;	//Not really a good place to do this, but keep consistent
	int			i;
	
	// Check for the existence of a parent
	if(parent != NULL) {
		if((parent->flags & OF_INIT) != OF_INIT){
			char objname[256];
			gl_verbose("evcharger_multitrip::init(): deferring initialization on %s", gl_name(parent, objname, 255));
			return 2; // defer
		}
		if( gl_object_isa(parent,"meter") ) {
			// attach meter variables to each circuit
			parent_string = "meter";
			struct {
				complex **var;
				char *varname;
			} map[] = {
			// local object name,	meter object name
				{&pCircuit_V,			"voltage_A"},	// assumes 2 and 3 follow immediately in memory
				{&pLine_I,				"current_A"},	// assumes 2 and 3(N) follow immediately in memory
				{&pPower,				"power_A"},		// assumes 2 and 3 follow immediately in memory
			};
			/// @todo use triplex property mapping instead of assuming memory order for meter variables (residential, low priority) (ticket #139)

			for (i=0; i<sizeof(map)/sizeof(map[0]); i++)
				*(map[i].var) = get_complex(parent,map[i].varname);

			gl_verbose("Voltages: %f|%f,%f|%f,%f|%f",pCircuit_V[0].Re(),pCircuit_V[0].Im(),pCircuit_V[1].Re(),pCircuit_V[1].Im(),pCircuit_V[2].Re(),pCircuit_V[2].Im());

			//Map status
			pMeterStatus = get_enum(parent,"service_status");

			//Check it
			if (pMeterStatus==NULL)
			{
				GL_THROW("EV Charger Multitrip failed to map powerflow status variable");
				/*  TROUBLESHOOT
				While attempting to map the service_status variable of the parent
				powerflow object, an error occurred.  Please try again.  If the error
				persists, please submit your code and a bug report via the trac website.
				*/
			}

			//Map phases
			set *phaseInfo;
			PROPERTY *tempProp;
			tempProp = gl_get_property(parent,"phases");

			if ((tempProp==NULL || tempProp->ptype!=PT_set))
			{
				GL_THROW("Unable to map phases property - ensure the parent is a meter or triplex_meter");
				/*  TROUBLESHOOT
				While attempting to map the phases property from the parent object, an error was encountered.
				Please check and make sure your parent object is a meter or triplex_meter inside the powerflow module and try
				again.  If the error persists, please submit your code and a bug report via the Trac website.
				*/
			}
			else
				phaseInfo = (set*)GETADDR(parent,tempProp);

			//Copy in so the code works
			phases = *phaseInfo;
			
		}
		else if ( gl_object_isa(parent,"triplex_meter") ) {
			parent_string = "triplex_meter";

			struct {
				complex **var;
				char *varname;
			} map[] = {
				// local object name,	meter object name
				{&pCircuit_V,			"voltage_12"},	// assumes 1N and 2N follow immediately in memory
				{&pLine_I,				"current_1"},	// assumes 2 and 3(N) follow immediately in memory
				{&pLine12,				"current_12"},	// maps current load 1-2 onto triplex load
				{&pPower,				"power_12"},	//assumes 2 and 1-2 follow immediately in memory
				/// @todo use triplex property mapping instead of assuming memory order for meter variables (residential, low priority) (ticket #139)
			};

			// attach meter variables to each circuit
			for (i=0; i<sizeof(map)/sizeof(map[0]); i++)
			{
				if ((*(map[i].var) = get_complex(parent,map[i].varname))==NULL)
				{
					GL_THROW("%s (%s:%d) does not implement triplex_meter variable %s for %s (inverter:%d)", 
					/*	TROUBLESHOOT
						The EV Charger Multitrip requires that the triplex_meter contains certain published properties in order to properly connect
						the inverter to the triplex-meter.  If the triplex_meter does not contain those properties, GridLAB-D may
						suffer fatal pointer errors.  If you encounter this error, please report it to the developers, along with
						the version of GridLAB-D that raised this error.
					*/
					parent->name?parent->name:"unnamed object", parent->oclass->name, parent->id, map[i].varname, obj->name?obj->name:"unnamed", obj->id);
				}
			}

			//Map status
			pMeterStatus = get_enum(parent,"service_status");

			//Check it
			if (pMeterStatus==NULL)
			{
				GL_THROW("EV Charger Multitrip failed to map powerflow status variable");
				//Defined above
			}

			//Map phases
			set *phaseInfo;
			PROPERTY *tempProp;
			tempProp = gl_get_property(parent,"phases");

			if ((tempProp==NULL || tempProp->ptype!=PT_set))
			{
				GL_THROW("Unable to map phases property - ensure the parent is a meter or triplex_meter");
				//Defined above
			}
			else
				phaseInfo = (set*)GETADDR(parent,tempProp);

			//Copy in so the code works
			phases = *phaseInfo;
			
		}
		else {
			GL_THROW("EV Charger Multitrip must have a meter or triplex meter as it's parent");
			/*  TROUBLESHOOT
			Check the parent object of the evcharer_multitrip.  The evcharer_multitrip is only able to be childed via a meter or 
			triplex meter when connecting into powerflow systems.  You can also choose to have no parent, in which
			case the evcharer_multitrip will be a stand-alone application using default voltage values for solving purposes.
			*/
		}
		
	}
	// if there is no parent, run it as an isolated
	else {
		parent_string = "none";

		gl_warning("EV Charger Multitrip:%d has no parent meter object defined; using static voltages", obj->id);
		
		// attach meter variables to each circuit in the default_meter
		pCircuit_V = &default_line_voltage[0];
		pLine_I = &default_line_current[0];

		//Attach meter status default
		pMeterStatus = &default_meter_status;
		default_meter_status = 1;

		// Declare all 3 phases
		phases = 0x0007;
	}
	
	// count the number of phases
	// split phase
	if ( (phases & 0x0010) == 0x0010) {
		number_of_phases_out = 1; 
	}
	// three phase
	else if ( (phases & 0x0007) == 0x0007 ) {
		number_of_phases_out = 3;
	}
	// two-phase
	else if ( ((phases & 0x0003) == 0x0003) || ((phases & 0x0005) == 0x0005) || ((phases & 0x0006) == 0x0006) ) {
		number_of_phases_out = 2;
	}
	// single phase
	else if ( ((phases & 0x0001) == 0x0001) || ((phases & 0x0002) == 0x0002) || ((phases & 0x0004) == 0x0004) ) {
		number_of_phases_out = 1;
	}
	// Throw an error
	else {
		//Never supposed to really get here
		GL_THROW("Invalid phase configuration specified!");
		/*  TROUBLESHOOT
		An invalid phase congifuration was specified when attaching to the "parent" object.  Please report this
		error.
		*/
	}
	
	// Get the global minimum timestep
	glob_min_timestep			= get_global_minimum_timestep();

	// If the minimum_timestep is more than 1s
	if (glob_min_timestep > 1)					// Now check us
	{
		off_nominal_time		= true;			// Set flag
		gl_verbose("evcharger_multitrip:%s - minimum_timestep set - problems may emerge",obj->name);
		/*  TROUBLESHOOT
		The evcharger detected that the forced minimum timestep feature is enabled.  This may cause
		issues with the evcharger schedule, especially if arrival/departure times are shorter than a
		minimum timestep value.
		*/
	}

	//	TODO: determine if a breaker is appropriate here or at the meter. Meter?
	
	// Trip CSV support
	if (vehicle_trips_csv_file[0] != '\0' && vehicle_trips_indexes_str[0] != '\0') {
		if(init_vehicle_trips()) {
			// 
			gl_verbose("Imported %d trips",vehicle_trips_length());
		}
		else {
			// 
			gl_warning("Could not import trips");
		}
	} else if (vehicle_data.vehicle_trip_start == NULL) {
		gl_warning("No trip information. Vehicle sitting in garage the whole time.");
		if (vehicle_trips_csv_file[0] == '\0') {
			gl_warning("No trips available.");
		} else if (vehicle_trips_indexes_str[0] != '\0') {
			gl_warning("No trips selected.");
		}
	}
	
	// No Trip CSV data - lets go default baby!
	if(vehicle_data.vehicle_trip_start == NULL) {
		// Trips - Set to NULL to begin!
		// new(vehicle_data.vehicle_trip_start) VehicleTrip();
		vehicle_data.vehicle_trip_start = (struct VehicleTrip*)malloc(sizeof(struct VehicleTrip));
		vehicle_data.vehicle_trip_end								= vehicle_data.vehicle_trip_start;
		vehicle_data.vehicle_trip_end->next							= vehicle_data.vehicle_trip_start;
		vehicle_data.vehicle_trip_end->trip_id						= 1;
		vehicle_data.vehicle_trip_end->months						= 0;
		vehicle_data.vehicle_trip_end->days							= 0;
		vehicle_data.vehicle_trip_end->travel_distance				= 15.0;
		vehicle_data.vehicle_trip_end->trip_time_to_destination		= 1800.0;
		vehicle_data.vehicle_trip_end->trip_time_at_destination		= 41400.0;
		vehicle_data.vehicle_trip_end->trip_time_to_home			= 1800.0;
		vehicle_data.vehicle_trip_end->trip_time_at_home			= 41400.0;
		vehicle_data.vehicle_trip_end->home_arrival_time			= 1700.0;
		vehicle_data.vehicle_trip_end->home_departure_time			= 430.0;
		vehicle_data.vehicle_trip_end->destination_arrival_time		= 500.0;
		vehicle_data.vehicle_trip_end->destination_departure_time	= 1630.0;
	}
	
	//	Convert it to a timestamp value
	init_time = gl_globalclock;

	//	Extract the relevant time information
	gl_localtime(init_time,&init_date);
	// What about timezones

	// Get the vehicle_trip that is applicable at the moment
	vehicle_data.vehicle_trip_current = get_vehicle_trip(init_time);
	if(vehicle_data.vehicle_trip_current == NULL) {
		trip_id = 0;
	} else {
		trip_id = vehicle_data.vehicle_trip_current->trip_id;
	}

	// Set the starting location and next transition timestamp
	vehicle_data.location = get_vehicle_location(init_time);
	vehicle_data.next_state_change = get_next_transition(init_time);

	// Errors
	// Battery size not set
	if (vehicle_data.battery_size <= 0.0) {
		GL_THROW("Battery size is not specified, nor are the mileage classification or efficiency!");
		/*  TROUBLESHOOT
		The battery size, mileage classification, and efficiency are all undefined.  At least two of these
		need to be defined to properly initialize the object.
		*/
	}
	// Mileage efficiency not set
	if (vehicle_data.mileage_efficiency <= 0.0)
	{
		GL_THROW("Mileage efficiency not specified, more information needed!");
		/*  TROUBLESHOOT
		A battery size was specified, but not a mileage classification or efficiency value.  One of these
		values is necessary to properly initialize the object.
		*/
	}

	// TODO: subset discharge for a trip (don't assume at home)
	// Determine state of charge - assume starts at 100% SOC at home initially, so adjust accordingly
	if ((vehicle_data.battery_capacity < 0.0) && (vehicle_data.battery_soc >= 0.0)) {
		// Populate current capacity based on SOC
		vehicle_data.battery_capacity = vehicle_data.battery_size * vehicle_data.battery_soc / 100.0;
	}

	// Check vice-versa
	if ((vehicle_data.battery_soc < 0.0) && (vehicle_data.battery_capacity >= 0.0)) {
		// Populate SOC
		vehicle_data.battery_soc = vehicle_data.battery_capacity / vehicle_data.battery_size * 100.0;
	}
	
	// Should be set, if was specified, otherwise, give us an init
	if ((vehicle_data.battery_soc < 0.0) || (vehicle_data.battery_capacity < 0.0)) {
		
		// default to 100% before adjusting
		vehicle_data.battery_soc = 100.0;
		vehicle_data.battery_capacity = vehicle_data.battery_size;
		
		// If at destination, deduct the trip to init_time
		if ( vehicle_data.location == VL_HOME_TO_DESTINATION ) {
			drive_electric_vehicle(vehicle_data.next_state_change - init_time, VL_HOME_TO_DESTINATION);
		}
		// If at destination, deduct a trip and then charge if needed for timeframe
		else if ( vehicle_data.location == VL_DESTINATION ) {
			drive_electric_vehicle(	vehicle_data.vehicle_trip_current->trip_time_to_destination, VL_HOME_TO_DESTINATION);
			charge_electric_vehicle( (vehicle_data.next_state_change - init_time), VL_DESTINATION);
		}
		// Must be destination to home - deduct full trip
		else if ( vehicle_data.location == VL_DESTINATION_TO_HOME ) {
			drive_electric_vehicle(vehicle_data.vehicle_trip_current->trip_time_to_destination, VL_HOME_TO_DESTINATION);
			charge_electric_vehicle(vehicle_data.vehicle_trip_current->trip_time_at_destination, VL_DESTINATION);
			drive_electric_vehicle( (vehicle_data.next_state_change - init_time), VL_DESTINATION_TO_HOME);
		}
	}

	// Check efficiency
	if ((vehicle_data.charge_efficiency<0) || (vehicle_data.charge_efficiency > 1.0)) {
		GL_THROW("Charger efficiency is outside of practical bounds!");
		/*  TROUBLESHOOT
		The charger can only be specified between 0% (0.0) and 100% (1.0) efficiency.
		Please limit the input to this range.
		*/
	}

	// Set the charge rate that we are starting at
	set_charge_rate(vehicle_data.next_state_change - init_time);
	// Sets the charger load, using the current charge_rate (W)
	// TODO: We need to have this to make it work, so we'll call update_load_power with a time
	// 			of 1s and energy of (charge_rate / 1000.0) * (1 / 3600.0)
	// update_load_power(1, (charge_rate / 1000.0) * (1 / 3600.0) );

	// Return 1
	return 1;
}

// @param	t0[TIMESTAMP]
// @param	t1[TIMESTAMP]
// @return	TIMESTAMP			
TIMESTAMP evcharger_multitrip::presync(TIMESTAMP t0, TIMESTAMP t1) {
	return TS_NEVER;
}

// @param	t0[TIMESTAMP]		Current timestamp
// @param	t1[TIMESTAMP]		Previous timestamp
// @return	TIMESTAMP			
TIMESTAMP evcharger_multitrip::sync(TIMESTAMP t0, TIMESTAMP t1) {
	OBJECT *obj = OBJECTHDR(this);
	double home_charge_energy;
	TIMESTAMP t2,tret, tdiff, time_to_next_state_change;
	VehicleTrip	*tmp_vehicle_trip;
	
	// Update the current vehicle
	vehicle_data.vehicle_trip_current = get_vehicle_trip(t1);
	if(vehicle_data.vehicle_trip_current == NULL) {
		trip_id = 0;
	} else {
		trip_id = vehicle_data.vehicle_trip_current->trip_id;
	}

	gl_verbose("%s SYNC @ %d (%d): prev_time:%d | t1:%d | t0:%d",obj->name,vehicle_data.location,trip_id,prev_time,t1,t0);
	
	// Lets output a warning if we are on a trip but have no trip listed.
	if( vehicle_data.location != VL_HOME && trip_id == 0 ) {
		gl_warning("Vehicle at %d but is not on a trip and time to next SC is %d. Showing all trips.",vehicle_data.location,(vehicle_data.next_state_change - t0));
		struct VehicleTrip	*trip = vehicle_data.vehicle_trip_start;
		do {
			gl_warning_vehicle_trip(trip);
			trip = trip->next;
		} while(trip != vehicle_data.vehicle_trip_start);
		GL_THROW("Vehicle at %d but is not on a trip. Showing all trips.",vehicle_data.location);
	}
	
	// Only perform an action if the time has changed and isn't first time
	// This is due to the impact on the energy in the battery of the EV.
	if ((prev_time != t1) && (t0 != 0)) {
		
		// Time step currently in play
		tdiff = t1 - t0;
		
		// Intialise to zero
		home_charge_energy = 0;

		// Time step to the state change for the electric vehicle
		time_to_next_state_change = vehicle_data.next_state_change - t0;
		gl_verbose("Time to next SC: %d",time_to_next_state_change);

		// Dependent on our location, figure out what to do
		switch (vehicle_data.location)
		{
			case VL_HOME:
				set_charge_rate(time_to_next_state_change);

				// If the new timestamp is at or above the point of change to driving to the destination we need to finish the charge and move onto the driving discharge
				if (t1 >= vehicle_data.next_state_change) {
					
					// Charge for remaining time before the next_state_change
					tdiff = vehicle_data.next_state_change - t0;					
					home_charge_energy = charge_electric_vehicle(tdiff, vehicle_data.location);

					// Transition to going to work
					vehicle_data.location = VL_HOME_TO_DESTINATION;
					
					// New time diff
					tdiff = t1 - vehicle_data.next_state_change;
					
					// Update the energy used in the intervening time
					drive_electric_vehicle(tdiff, vehicle_data.location);
					
					// Update the next transition
					vehicle_data.next_state_change = get_next_transition(t1);
				}
				// Otherwise we are still charging the battery
				else {
					// Charge it
					home_charge_energy = charge_electric_vehicle(tdiff, vehicle_data.location);
					// Confirm that we are still where we think we are
					vehicle_data.location = VL_HOME;
				}
				break;

			case VL_HOME_TO_DESTINATION:
			
				// Set the charge_rate to maximum as we are away from home
				charge_rate = vehicle_data.max_charge_rate;
				
				// If the new timestamp is at or above the point of change to being at the destination we need to finish the discharge and move onto the destination charging (if applicable)
				if (t1 >= vehicle_data.next_state_change) {
					
					// Charge for remaining time before the next_state_change
					tdiff = vehicle_data.next_state_change - t0;

					// Update the energy used in the intervening time
					drive_electric_vehicle(tdiff, vehicle_data.location);

					// Transition to going to work
					vehicle_data.location = VL_DESTINATION;

					// Deal with zero time at destination
					if((int)(vehicle_data.vehicle_trip_current->trip_time_at_destination) == 0) {
						vehicle_data.location = VL_DESTINATION_TO_HOME;
					}

					// New time diff
					tdiff = t1 - vehicle_data.next_state_change;
					
					// Charge it - if possible
					charge_electric_vehicle(tdiff, vehicle_data.location);
					
					// Update the next transition
					vehicle_data.next_state_change = get_next_transition(t1);
				}
				// Otherwise we are still driving
				else {
					// Drive it
					drive_electric_vehicle(tdiff, VL_HOME_TO_DESTINATION);
					home_charge_energy = 0;
					// Confirm that we are still where we think we are
					vehicle_data.location = VL_HOME_TO_DESTINATION;
				}
				break;

			case VL_DESTINATION:
			
				// Set the charge_rate to maximum as we are away from home
				charge_rate = vehicle_data.max_charge_rate;
			
				// If the new timestamp is at or above the point of change to driving to the destination we need to finish the charge and move onto the driving discharge
				if (t1 >= vehicle_data.next_state_change) {
					
					// Charge for remaining time before the next_state_change
					tdiff = vehicle_data.next_state_change - t0;

					// Charge it
					charge_electric_vehicle(tdiff, vehicle_data.location);

					// Transition to going to work
					vehicle_data.location = VL_DESTINATION_TO_HOME;

					// Next time diff
					tdiff = t1 - vehicle_data.next_state_change;
					
					// Update the energy used in the intervening time
					drive_electric_vehicle(tdiff, vehicle_data.location);
					
					// Update the next transition
					vehicle_data.next_state_change = get_next_transition(t1);
				}
				// Otherwise we are still charging the battery
				else {
					// Charge it
					charge_electric_vehicle(tdiff, vehicle_data.location);

					// Confirm that we are still where we think we are
					vehicle_data.location = VL_DESTINATION;
				}
				break;

			case VL_DESTINATION_TO_HOME:
				
				// If the new timestamp is at or above the point of change to arriving at home we need to finish the discharge and move onto the home charging (if applicable)
				//  or determine if a new trip has started...
				if (t1 >= vehicle_data.next_state_change) {					
					// Charge for remaining time before the next_state_change
					tdiff = vehicle_data.next_state_change - t0;
					
					// Update the energy used in the intervening time
					drive_electric_vehicle(tdiff, vehicle_data.location);

					// Transition to being at home
					vehicle_data.location = VL_HOME;
					
					// New time diff
					tdiff = t1 - vehicle_data.next_state_change;
					
					// Check if we need to change this again due to a trip starting as soon as we get home (drop off style)
					tmp_vehicle_trip = get_next_vehicle_trip(t1);
					gl_verbose("Next vehicle trip is %d to check its home departure time of %f against %d home arrival time of %f",tmp_vehicle_trip->trip_id,tmp_vehicle_trip->home_departure_time,vehicle_data.vehicle_trip_current->trip_id,vehicle_data.vehicle_trip_current->home_arrival_time);

					// Test needs to also check days and months to be coincident
					if(vehicle_data.vehicle_trip_current->home_arrival_time == tmp_vehicle_trip->home_departure_time
						&& vehicle_data.vehicle_trip_current->months == tmp_vehicle_trip->months
						&& vehicle_data.vehicle_trip_current->days == tmp_vehicle_trip->days
						) {
						// We need to drive for a bit if tdiff greater than 0
						//  and set the new vehicle trip
						vehicle_data.vehicle_trip_current = tmp_vehicle_trip;
						trip_id = vehicle_data.vehicle_trip_current->trip_id;
						
						// Transition to driving to destination
						vehicle_data.location = VL_HOME_TO_DESTINATION;

						// Update the energy used in the intervening time
						drive_electric_vehicle(tdiff, vehicle_data.location);

					} else {
						// Set the charge rate
						set_charge_rate(time_to_next_state_change);

						// Charge it - if possible
						home_charge_energy = charge_electric_vehicle(tdiff, vehicle_data.location);
					}
					// Update the next transition so that we can set the charge rate
					vehicle_data.next_state_change = get_next_transition(t1);
				}
				// Otherwise we are still driving
				else {
					// Drive it
					drive_electric_vehicle(tdiff, VL_DESTINATION_TO_HOME);
					home_charge_energy = 0;
					// Confirm that we are still where we think we are
					vehicle_data.location = VL_DESTINATION_TO_HOME;
				}
				break;

			case VL_UNKNOWN:
			default:
				GL_THROW("Vehicle is at an unknown location!");
				/*  TROUBLESHOOT
				The vehicle somehow is in an unknown location.  Please try again.  If the error persists,
				please submit your GLM and a bug report via the Trac website.
				*/
		}
		
		// Update the load power
		gl_verbose("%s home_charge_energy is %f\n", obj->name, home_charge_energy);
		update_load_power(t1 - t0, home_charge_energy);

		// Update the pointer
		prev_time = t1;
	}
	// Otherwise just set the power required from the last iteration
	else if (t0 != 0) {
		pLine_I[0] += load_data.phaseA_I;
		pLine_I[1] += load_data.phaseB_I;
		pLine_I[2] += load_data.phaseC_I;
	}

	// TODO: remove load as it is an enduse component
	// Update enduse parameter - assumes only constant power right now
	// load.total = load.power;

	// Pull the next state transition
	tret = vehicle_data.next_state_change;

	// Minimum timestep check
	if (off_nominal_time == true)
	{
		// See where our next "expected" time is
		t2 = t1 + glob_min_timestep;
		if (tret < t2)	// tret is less than the next "expected" timestep
			tret = t2;	// Unfortunately, GridLAB-D is "special" and doesn't know how to handle this with min timesteps.  We have to fix it.
	}

	gl_verbose("%s finished trip %d at %d with SOC of %f and returning %d", obj->name, trip_id, vehicle_data.location, vehicle_data.battery_soc,tret);
	
	
	return tret;
}

// Postsync is called when the clock needs to advance on the second top-down pass
// @param	t0[TIMESTAMP]
// @param	t1[TIMESTAMP]
// @return	TIMESTAMP
TIMESTAMP evcharger_multitrip::postsync(TIMESTAMP t0, TIMESTAMP t1) {
	OBJECT *obj = OBJECTHDR(this);
	TIMESTAMP t2 = TS_NEVER;		//By default, we're done forever! ? Really?

	gl_verbose("%s POSTSYNC @ %d (%d): prev_time:%d | t1:%d | t0:%d",obj->name,vehicle_data.location,trip_id,prev_time,t1,t0);

	// Only perform an action if the time has changed and isn't first time
	// prev_time is updated in sync so can't use that.
	if (/*(prev_time_postsync != t1) &&*/ (t0 != 0)) {
		// TODO: Work this out properly - something isn't happening here for the initialisation point.
		// Triplex
		if ((phases & 0x0010) == 0x0010) {
			*pLine12 -= load_data.phaseA_I;	//Remove from current12
		}
		// Variation of three-phase
		else {
			gl_verbose("PS.B:M.Currents: %f|%f,%f|%f,%f|%f",pLine_I[0].Re(),pLine_I[0].Im(),pLine_I[1].Re(),pLine_I[1].Im(),pLine_I[2].Re(),pLine_I[2].Im());
	
			pLine_I[0] -= load_data.phaseA_I;
			pLine_I[1] -= load_data.phaseB_I;
			pLine_I[2] -= load_data.phaseC_I;

			gl_verbose("PS.A:M.Currents: %f|%f,%f|%f,%f|%f",pLine_I[0].Re(),pLine_I[0].Im(),pLine_I[1].Re(),pLine_I[1].Im(),pLine_I[2].Re(),pLine_I[2].Im());
			gl_verbose("PS.CL:%f|%f,%f|%f,%f|%f",load_data.phaseA_I.Re(),load_data.phaseA_I.Im(),load_data.phaseB_I.Re(),load_data.phaseB_I.Im(),load_data.phaseC_I.Re(),load_data.phaseC_I.Im());
		}
	}
	prev_time_postsync = t1;
	return vehicle_data.next_state_change; /* return t2>t1 on success, t2=t1 for retry, t2<t1 on failure */
}

// @return int				0 or 1 based on if the object is of type classname
int evcharger_multitrip::isa(char *classname)
{
	return (strcmp(classname,"evcharger_multitrip")==0);
}

// Returns the globally configured minimum timestep in use.
// @return TIMESTAMP		global_minimum_timestep
TIMESTAMP evcharger_multitrip::get_global_minimum_timestep() {

	char		temp_buff[128];

	//	Get global_minimum_timestep value and set the appropriate flag
	//	Retrieve the global value, only does so as a text string for some reason
	// 	So why not use a native method to get the TIMESTAMP?!?!?
	gl_global_getvar("minimum_timestep",temp_buff,sizeof(temp_buff));
	
	return (TIMESTAMP)(atof(temp_buff));
}

// Initialises the vehicle trips
// TODO: Tidy up function variables
// @return int				true or false based on getting the data
int evcharger_multitrip::init_vehicle_trips() {

	struct ColumnList {
		char	name[128];
		struct	ColumnList *next;
	};													///< ColumnList structure

	VehicleTrip			*temp_vehicle_trip;				///< Temp element for Vehicle trip to add to the vehicle_data.vehicle_trips
	FILE				*vehicle_trip_csv_file_handler;	///< File handler for the vehicle_trip_csv file
	struct ColumnList	*column_first = 0, *column_last = 0, *column_current = 0;
														///< ColumnList pointers
	
	char				line[1024];						///< Line of CSV
	char				buffer[1024];					///< The buffer for manipulation of a line
	char				buffer2[128];					///< The buffer for manipulation of a line
	char				*token = 0;						///< Token for data
	char				*next;							///< state for strok_s
	const char			delim[] = ",\n\r\t";		///< delimiters to tokenise
	
	int					has_cols	= 0;				///< Boolean to flag the columns of the CSV as obtained and populated into the column_list
	int					trip_id		= 0;				///< trip_id integer
	int					column_count = 0;				///< Total count of columns
	int					col = 0;						///< Current column counter for looping through all columns
	int					row = 0;

	// First we need to create the vehicle_trips linked list based on the indexes provided. Then we go through and populate them
	memset(buffer, 0, 1024);							///< This overwrites the buffer with null values
	strncpy(buffer, vehicle_trips_indexes_str, 1024);	///< Copies the vehicle_trip_csv_file_handler into the buffer for use

	token = strtok_s(buffer, delim, &next);			///< Now use the buffer to break up by commas, turn into integers, and then add to the list.
	// Move through each
	while( token != NULL ) {
		memset(buffer2, 0, 128);
		strcpy(buffer2,token);

		trip_id = (int)(atol(buffer2));					///< Grab the index

		// Always work with the last element
		// No vehicle_trips
		if(vehicle_data.vehicle_trip_start == 0) {
			vehicle_data.vehicle_trip_start		= new VehicleTrip;
			vehicle_data.vehicle_trip_end		= vehicle_data.vehicle_trip_start;
			vehicle_data.vehicle_trip_end->next	= vehicle_data.vehicle_trip_start;
		}
		// vehicle trips exist, so create a new one
		else {
			vehicle_data.vehicle_trip_end->next = new VehicleTrip;
			vehicle_data.vehicle_trip_end		= vehicle_data.vehicle_trip_end->next;
			vehicle_data.vehicle_trip_end->next = vehicle_data.vehicle_trip_start;
		}
		// Set all default values
		// Use negatives for doubles that would otherwise be NULL
		vehicle_data.vehicle_trip_end->trip_id						= trip_id;
		vehicle_data.vehicle_trip_end->months						= 0;
		vehicle_data.vehicle_trip_end->days							= 0;
		vehicle_data.vehicle_trip_end->travel_distance				= -1;
		vehicle_data.vehicle_trip_end->trip_time_to_destination		= -1;
		vehicle_data.vehicle_trip_end->trip_time_at_destination		= -1;
		vehicle_data.vehicle_trip_end->trip_time_to_home			= -1;
		vehicle_data.vehicle_trip_end->trip_time_at_home			= -1;
		vehicle_data.vehicle_trip_end->home_departure_time			= -1;
		vehicle_data.vehicle_trip_end->home_arrival_time			= -1;
		vehicle_data.vehicle_trip_end->destination_departure_time	= -1;
		vehicle_data.vehicle_trip_end->destination_arrival_time		= -1;

		token = strtok_s(NULL, delim, &next);
	}
	temp_vehicle_trip = vehicle_data.vehicle_trip_start;

	// Open the file
	vehicle_trip_csv_file_handler = fopen(vehicle_trips_csv_file, "r");
	// If invalid return 0 and a warning
	if(vehicle_trip_csv_file_handler == 0){
		GL_THROW("Could not open \'%s\' (\'vehicle_trips_csv_file\') for input!\n\tCheck for Windows or Linux paths.\n\tUse double quotes.", (char*)vehicle_trips_csv_file);
		/* TROUBLESHOOT
			The specified input file could not be opened for reading.  Verify that no
			other applications are using that file, double-check the input model, and
			re-run GridLAB-D.
		*/
		return 0;
	}
	// Otherwise do the reading and creating of trips!
	else {
		// Go through each line
		while(fgets(line, 1024, vehicle_trip_csv_file_handler) > 0){

			// columns are to use lowercase only
			if(has_cols == 0 && line[0] >=97 && line[0] < 123){
			
				// expected format: x,y,z\n
				// This overwrites the buffer with null values, and then copies in the line
				memset(buffer, 0, 1024);
				strncpy(buffer, line, 1023);

				token = strtok_s(buffer, delim, &next);						///< Now use the buffer to break up by commas, turn into integers, and then add to the list.
			
				// No null values allowed in the CSV Header.
				while( token != NULL ) {
					memset(buffer2, 0, 128);
					strcpy(buffer2,token);

					// Create the new column
					if(column_first == 0){
						column_first		= new ColumnList;
						column_first->next	= column_first;
						column_last			= column_first;
					} else {
						column_last->next	= new ColumnList;
						column_last			= column_last->next;
						column_last->next	= column_first;
					}
				
					// Populate it
					strncpy(column_last->name, buffer2, 128);
					column_count++;
					
					// Go to the next token
					token = strtok_s(NULL, delim, &next);
				}
				has_cols = 1;
			}
			// Valid data lines starts with a number (0-9) (in fact, they all are)
			// 	and columns have been populated
			else if (has_cols == 1 && line[0] >= 48 && line[0] < 58) {
				
				row++;
				
				memset(buffer, 0, 1024);		// Wipe it
				strncpy(buffer, line, 1023);	// removes the trailing /n but assumes only /n but does provide for a null character as the 1024th character.

				token = strtok_s(buffer, delim, &next);	// get the tokens
			
				// blank line - continue
				if(token == NULL){
					continue;
				}
				// First token is the trip_id
				else {
					col = 0;
					column_current = column_first;
				
					trip_id = (int)(atol(token));
				
					// If the trip is the one we are looking for then awesome
					if(temp_vehicle_trip->trip_id == trip_id) {
						
						token = strtok_s(NULL, delim, &next);
						col++;
						column_current = column_current->next;


						while(token != NULL && col < column_count && column_current != NULL) {
							
							memset(buffer2, 0, 128);		// Wipe it
							strcpy(buffer2, token);			// Set it
						
							// Now we need to look at the columns
							if(column_current == 0) {
								column_current = column_first;
							}
						
							// Populate the details
							if(strcmp(column_current->name,"months") == 0) {
								temp_vehicle_trip->months = (int)(atof(buffer2));
							} else if(strcmp(column_current->name,"days") == 0) {
								temp_vehicle_trip->days = (int)(atof(buffer2));
							} else if(strcmp(column_current->name,"travel_distance") == 0) {
								temp_vehicle_trip->travel_distance = atof(buffer2);
							} else if(strcmp(column_current->name,"trip_time_to_destination") == 0) {
								temp_vehicle_trip->trip_time_to_destination = atof(buffer2);
							} else if(strcmp(column_current->name,"trip_time_at_destination") == 0) {
								temp_vehicle_trip->trip_time_at_destination = atof(buffer2);
							} else if(strcmp(column_current->name,"trip_time_to_home") == 0) {
								temp_vehicle_trip->trip_time_to_home = atof(buffer2);
							} else if(strcmp(column_current->name,"trip_time_at_home") == 0) {
								temp_vehicle_trip->trip_time_at_home = atof(buffer2);
							} else if(strcmp(column_current->name,"home_departure_time") == 0) {
								temp_vehicle_trip->home_departure_time = atof(buffer2);
								// Deal with decimal based time of day (eg: Excel format)
								if( 0 < temp_vehicle_trip->home_departure_time && temp_vehicle_trip->home_departure_time < 1 ) {
									temp_vehicle_trip->home_departure_time = double_seconds_to_double_time( (temp_vehicle_trip->home_departure_time * 1440) * 60 );
								}
							} else if(strcmp(column_current->name,"home_arrival_time") == 0) {
								temp_vehicle_trip->home_arrival_time = atof(buffer2);
								// Deal with decimal based time of day (eg: Excel format)
								if( 0 < temp_vehicle_trip->home_arrival_time && temp_vehicle_trip->home_arrival_time < 1 ) {
									temp_vehicle_trip->home_arrival_time = double_seconds_to_double_time( (temp_vehicle_trip->home_arrival_time * 1440) * 60 );
								}
							} else if(strcmp(column_current->name,"destination_departure_time") == 0) {
								temp_vehicle_trip->destination_departure_time = atof(buffer2);
								// Deal with decimal based time of day (eg: Excel format)
								if( 0 < temp_vehicle_trip->destination_departure_time && temp_vehicle_trip->destination_departure_time < 1 ) {
									temp_vehicle_trip->destination_departure_time = double_seconds_to_double_time( (temp_vehicle_trip->destination_departure_time * 1440) * 60 );
								}
							} else if(strcmp(column_current->name,"destination_arrival_time") == 0) {
								temp_vehicle_trip->destination_arrival_time = atof(buffer2);
								// Deal with decimal based time of day (eg: Excel format)
								if( 0 < temp_vehicle_trip->destination_arrival_time && temp_vehicle_trip->destination_arrival_time < 1 ) {
									temp_vehicle_trip->destination_arrival_time = double_seconds_to_double_time( (temp_vehicle_trip->destination_arrival_time * 1440) * 60 );
								}
							} else if(strcmp(column_current->name,"destination_has_charger") == 0) {
								temp_vehicle_trip->destination_has_charger = (int)(atof(buffer2));
							}

							// Go to the next token
							token = strtok_s(NULL, delim, &next);
							col++;
							column_current = column_current->next;
						}
					
						// TODO: move to vaidation step in the ev_charger::init function
						complete_trip_details(temp_vehicle_trip);
						
						// Now we need to deal with the next one
						temp_vehicle_trip = temp_vehicle_trip->next;
					}
				}
			}
		}
	
	}
	// Close the file
	fclose(vehicle_trip_csv_file_handler);
	
	// All good so return 1
	return 1;
}

// The VehicleTrip linked list is kept with the end element referencing the start element and these
// 		are held in cache.
// @return int				number of VehicleTrips
int evcharger_multitrip::vehicle_trips_length() {
	VehicleTrip	*trip;
	int			i;

	trip = vehicle_data.vehicle_trip_start;
	i = 0;
	if(trip != NULL) {
		do {
			i++;
			trip = trip->next;
		} while(trip != vehicle_data.vehicle_trip_start);
	}
	
	return i;
}

// Updates a trips details where they are blank (eg: departure times, arrival times or durations)
// @param	trip[struct VehicleTrip*]		Pointer to a VehicleTrip
// @return	int								0|1 depending upon success. Can then be used to validate
int evcharger_multitrip::complete_trip_details(struct VehicleTrip *trip) {
	
	// Update the details based on calculations where there is null data & create warnings if need be
	if(trip->trip_time_to_destination == -1) {
		if( trip->home_departure_time != -1 && trip->destination_arrival_time != -1 ) {
			trip->trip_time_to_destination = double_time_to_double_seconds(trip->destination_arrival_time) - double_time_to_double_seconds(trip->home_departure_time);
			// This portion of the trip is overnight - need to account for this
			if( trip->home_departure_time > trip->destination_arrival_time ) {
				trip->trip_time_to_destination += 86400.0;	// Add twenty four hours
			}
		}
		// Not enough data
		else {
			gl_warning("Cannot determine the trip time to destination");
			/*  TROUBLESHOOT
			The value for trip_time_to_destination was not set, and calculations could not be made.
			*/
		}
	} else { gl_verbose("%d trip_time_to_destination is not null",trip->trip_id); }
	if(trip->trip_time_at_destination == -1) {
		if( trip->destination_arrival_time != -1 && trip->destination_departure_time != -1 ) {
			trip->trip_time_at_destination = double_time_to_double_seconds(trip->destination_departure_time) - double_time_to_double_seconds(trip->destination_arrival_time);
			// This portion of the trip is overnight - need to account for this
			if( trip->destination_arrival_time > trip->destination_departure_time ) {
				trip->trip_time_at_destination += 86400.0;	// Add twenty four hours
			}
		}
		// Not enough data
		else {
			gl_warning("Cannot determine the trip time at destination");
			/*  TROUBLESHOOT
			The value for trip_time_at_destination was not set, and calculations could not be made.
			*/
		}
	} else { gl_verbose("%d trip_time_at_destination is not null",trip->trip_id); }
	if(trip->trip_time_to_home == -1) {
		if( trip->destination_departure_time != -1 && trip->home_arrival_time != -1 ) {
			trip->trip_time_to_home = double_time_to_double_seconds(trip->home_arrival_time) - double_time_to_double_seconds(trip->destination_departure_time);
			// This portion of the trip is overnight - need to account for this
			if( trip->destination_departure_time > trip->home_arrival_time ) {
				trip->trip_time_to_home += 86400.0;	// Add twenty four hours
			}
		}
		// Not enough data
		else {
			gl_warning("Cannot determine the trip time to home");
			/*  TROUBLESHOOT
			The value for trip_time_to_home was not set, and calculations could not be made.
			*/
		}
	} else { gl_verbose("%d trip_time_to_home is not null",trip->trip_id); }
	if(trip->trip_time_at_home == -1) {
		if( trip->home_arrival_time != -1 && trip->home_departure_time != -1 ) {
			trip->trip_time_at_home = double_time_to_double_seconds(trip->home_departure_time) - double_time_to_double_seconds(trip->home_arrival_time);
			// This portion of the trip is overnight - need to account for this
			if( trip->home_arrival_time > trip->home_departure_time ) {
				trip->trip_time_at_home += 86400.0;	// Add twenty four hours
			}
		}
		// Not enough data
		else {
			gl_warning("Cannot determine the trip time at home");
			/*  TROUBLESHOOT
			The value for trip_time_at_home was not set, and calculations could not be made.
			*/
		}
	} else { gl_verbose("%d trip_time_at_home is not null",trip->trip_id); }
	if(trip->home_departure_time == -1) {
		// Can determine departure by arrival at home + time at home
		if (trip->home_arrival_time != -1 && trip->trip_time_at_home != -1) {
			trip->home_departure_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->home_arrival_time) + trip->trip_time_at_home,86400.0));
		}
		// Can determine departure by arrival at destination - time to destination
		else if (trip->trip_time_to_destination != -1 && trip->destination_arrival_time != -1) {
			trip->home_departure_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->destination_arrival_time) - trip->trip_time_to_destination + 86400.0,86400.0));
		}
		else {
			gl_warning("Cannot determine the home departure time");
			/*  TROUBLESHOOT
			The value for home_departure_time was not set, and calculations could not be made.
			*/
		}
	}
	if(trip->home_arrival_time == -1) {
		// Can determine arrival by departure from home - time at home
		if (trip->home_departure_time != -1 && trip->trip_time_at_home != -1) {
			trip->home_arrival_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->home_departure_time) - trip->trip_time_at_home + 86400.0,86400.0));
		}
		// Can determine arrival by departure from destination + time to home
		else if (trip->trip_time_to_home != -1 && trip->destination_departure_time != -1) {
			trip->home_arrival_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->destination_departure_time) + trip->trip_time_to_home,86400.0));
		}
		else {
			gl_warning("Cannot determine the home arrival time");
			/*  TROUBLESHOOT
			The value for home_arrival_time was not set, and calculations could not be made.
			*/
		}
	}
	if(trip->destination_departure_time == -1) {
		// Can determine departure by arrival at destination + time at destination
		if (trip->destination_arrival_time != -1 && trip->trip_time_at_destination != -1) {
			trip->destination_departure_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->destination_arrival_time) + trip->trip_time_at_destination,86400.0));
		}
		// Can determine departure by arrival at home - time to home
		else if (trip->trip_time_to_home != -1 && trip->home_arrival_time != -1) {
			trip->destination_departure_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->home_arrival_time) - trip->trip_time_to_home + 86400.0,86400.0));
		}
		else {
			gl_warning("Cannot determine the destination departure time");
			/*  TROUBLESHOOT
			The value for destination_departure_time was not set, and calculations could not be made.
			*/
		}

	}
	if(trip->destination_arrival_time == -1) {
		// Can determine arrival by departure from destination - time at destination
		if (trip->destination_departure_time != -1 && trip->trip_time_at_destination != -1) {
			trip->destination_arrival_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->destination_departure_time) - trip->trip_time_at_destination + 86400.0,86400.0));
		}
		// Can determine arrival by departure from home + time to destination
		else if (trip->trip_time_to_destination != -1 && trip->home_departure_time != -1) {
			trip->destination_arrival_time = double_seconds_to_double_time(fmod(double_time_to_double_seconds(trip->home_departure_time) + trip->trip_time_to_destination,86400.0));
		}
		else {
			gl_warning("Cannot determine the destination arrival time");
			/*  TROUBLESHOOT
			The value for destination_arrival_time was not set, and calculations could not be made.
			*/
		}
	}
	
	// TODO: return 0 if any of the above 8 elements are NULL
	return 1;
}

// The VehicleTrip linked list is kept with the end element referencing the start element and these
// 		are held in cache.
// @param	t[TIMESTAMP]			The timestamp "t" to use to look up which trip is the current valid one.
// @return	VehicleTrip*			The current trip, based on the idea of no overlap (ie: uses first trip with valid month & day of week that isn't currently at home). If NULL then at home
struct VehicleTrip *evcharger_multitrip::get_vehicle_trip(TIMESTAMP t){
	VehicleTrip	*tmp_vehicle_trip, *return_vehicle_trip;
	DATETIME	t_date;
	double		t_seconds, t_seconds_comparison, home_arrival_time_seconds, home_departure_time_seconds;
	int 		overnight;

	// in a DATETIME format for each of comparisons
	gl_localtime(t,&t_date);
	t_seconds = datetime_time_to_double_seconds(&t_date);

	// gl_warning("Checking against time of day in seconds of: %f", t_seconds);

	// Go through the VehicleTrip objects looking for the one that first matches the current datetime
	//   it will only match if the day and month are valid and the vehicle is not currently at home
	//   if it returns NULL then the car is at home and thus charging
	tmp_vehicle_trip	= vehicle_data.vehicle_trip_start;
	return_vehicle_trip	= NULL;
	do {

		// Trip must be valid for this month and day of week
		if( vehicle_trip_valid_day_of_week(t_date.weekday,tmp_vehicle_trip) && vehicle_trip_valid_month_of_year(t_date.month,tmp_vehicle_trip) ) {
			
			// not between home_arrival_time and home_departure_time accounting for overnight stays
			// Rotate times so that home_arrival_time is always at 0, thus we are then checking if t_seconds_comparison >= home_departure_time_seconds
			home_arrival_time_seconds	= fmod(double_time_to_double_seconds(tmp_vehicle_trip->home_arrival_time) + (86400.0-double_time_to_double_seconds(tmp_vehicle_trip->home_arrival_time)),86400.0);
			home_departure_time_seconds	= fmod(double_time_to_double_seconds(tmp_vehicle_trip->home_departure_time) + (86400.0-double_time_to_double_seconds(tmp_vehicle_trip->home_arrival_time)),86400.0);
			t_seconds_comparison		= fmod(t_seconds + (86400.0-double_time_to_double_seconds(tmp_vehicle_trip->home_arrival_time)),86400.0);
			
			// Rotation based on making the arrival time 0 -> fmod(time + (86400.0-home_arrival_time_seconds),86400.0)
			// Return if it's just arrived as it has priority
			if( t_seconds_comparison == home_arrival_time_seconds ) {
				return tmp_vehicle_trip;
			}
			// Otherwise if you're on the trip that's awesome too, but we'll check the rest first.
			else if( t_seconds_comparison >= home_departure_time_seconds ) {
				return_vehicle_trip = tmp_vehicle_trip;
			}
		}
		tmp_vehicle_trip = tmp_vehicle_trip->next;
	} while(tmp_vehicle_trip != vehicle_data.vehicle_trip_start);
	
	// Return NULL if you are at home, or the next_vehicle_trip if not.
	return return_vehicle_trip;
}

// The VehicleTrip linked list is kept with the end element referencing the start element and these
// 		are held in cache.
// @param	t[TIMESTAMP]			The timestamp "t" to use to look up which trip is the current valid one.
// @return	VehicleTrip*			The current trip, based on the idea of no overlap (ie: uses first trip with valid month & day of week that isn't currently at home) or the next valid trip if it isn't on one at the moment.
struct VehicleTrip *evcharger_multitrip::get_next_vehicle_trip(TIMESTAMP t){
	VehicleTrip	*next_vehicle_trip, *tmp_vehicle_trip;
	TIMESTAMP	t_next_vehicle_trip, t_tmp_vehicle_trip;					// First valid timestamps of the trips
	DATETIME	t_date,t_next_vehicle_trip_date,t_tmp_vehicle_trip_date;	
	double		t_seconds;
	double		tmp_vehicle_trip_d, transition_offset_d, home_arrival_time_d, home_departure_time_d, destination_arrival_time_d, destination_departure_time_d;
	int 		i;

	gl_localtime(t,&t_date);
	t_next_vehicle_trip = t + (TIMESTAMP)(86400.0*366); // increment by a full year to begin with
	t_tmp_vehicle_trip	= t;							// Start at t for this - will be set on the first cycle through
	next_vehicle_trip	= NULL;
	
	// Cycle through each day and then cycle through each of the vehicle_trips
	// Only increment the day if there is no option for that day
	for(i = 0; (i < 366 && next_vehicle_trip == NULL ); i++) {

		// Update the test time for each day of the test
		t_tmp_vehicle_trip = t + (TIMESTAMP)(i * 86400.0);
		if ( i > 0 ) {
			// Reset to the zero hour of the day if it is not the day of "t" being tested
			t_tmp_vehicle_trip -= (TIMESTAMP)(datetime_time_to_double_seconds(&t_date));

			// Timezone difference offset
			gl_localtime(t_tmp_vehicle_trip,&t_tmp_vehicle_trip_date);
			if (t_date.is_dst && !t_tmp_vehicle_trip_date.is_dst) {
				t_tmp_vehicle_trip += 3600;
			}
			else if (!t_date.is_dst && t_tmp_vehicle_trip_date.is_dst) {
				t_tmp_vehicle_trip -= 3600;
			}
		}
		// Extract to a local DATETIME object for verification of month and day
		gl_localtime(t_tmp_vehicle_trip,&t_tmp_vehicle_trip_date);

		// Start at the start
		tmp_vehicle_trip = vehicle_data.vehicle_trip_start;

		// For each vehicle_trip we want to see which is the closest
		// If no valid trips for a given day, next_vehicle_trip will still be NULL and so we will increment the day
		do {

			// Only check if the month and day is valid for the trip
			if(vehicle_trip_valid_day_of_week(t_tmp_vehicle_trip_date.weekday,tmp_vehicle_trip) && vehicle_trip_valid_month_of_year(t_tmp_vehicle_trip_date.month,tmp_vehicle_trip)) {

				// DateTime of the test time (t or midnight of a following day)
				tmp_vehicle_trip_d		 		= datetime_time_to_double_seconds(&t_tmp_vehicle_trip_date); 
				// Do the same for each of the times for a trip. A valid trip only occurs if the time tmp_vehicle_trip_d is not at home
				home_arrival_time_d				= double_time_to_double_seconds(tmp_vehicle_trip->home_arrival_time);
				home_departure_time_d			= double_time_to_double_seconds(tmp_vehicle_trip->home_departure_time);
				destination_arrival_time_d		= double_time_to_double_seconds(tmp_vehicle_trip->destination_arrival_time);
				destination_departure_time_d	= double_time_to_double_seconds(tmp_vehicle_trip->destination_departure_time);
				transition_offset_d				= 60.0*60.0*24.0; // default to twenty four hours
			
				// Always has to be after the next_transition_start_d but also closer to the tmp_vehicle_trip_d time
				if(home_arrival_time_d > tmp_vehicle_trip_d && (home_arrival_time_d - tmp_vehicle_trip_d) < transition_offset_d ) {
					transition_offset_d = home_arrival_time_d - tmp_vehicle_trip_d;
				}
				if(home_departure_time_d > tmp_vehicle_trip_d && (home_departure_time_d - tmp_vehicle_trip_d) < transition_offset_d ) {
					transition_offset_d = home_departure_time_d - tmp_vehicle_trip_d;
				}
				if(destination_arrival_time_d > tmp_vehicle_trip_d && (destination_arrival_time_d - tmp_vehicle_trip_d) < transition_offset_d ) {
					transition_offset_d = destination_arrival_time_d - tmp_vehicle_trip_d;
				}
				if(destination_departure_time_d > tmp_vehicle_trip_d && (destination_departure_time_d - tmp_vehicle_trip_d) < transition_offset_d ) {
					transition_offset_d = destination_departure_time_d - tmp_vehicle_trip_d;
				}
				// 
				
				// The t_next_vehicle_trip must be within the day ( tmp_vehicle_trip_d + transition_offset_d < 86400.0 )
				// 	and be closer than the other t_next_vehicle_trip
				if( ( tmp_vehicle_trip_d + transition_offset_d < 86400.0 ) && ( (t_tmp_vehicle_trip + (TIMESTAMP)(transition_offset_d) ) < t_next_vehicle_trip ) ) {
					next_vehicle_trip	= tmp_vehicle_trip;
					t_next_vehicle_trip	= (t_tmp_vehicle_trip + (TIMESTAMP)(transition_offset_d));
				}
				
			}
			
			// Go to the next trip
			tmp_vehicle_trip = tmp_vehicle_trip->next;
		} while(tmp_vehicle_trip != vehicle_data.vehicle_trip_start);
	}
	
	return next_vehicle_trip;
}

// This is tricky with multiple trips for different days-of-week and months-of-year.
// @return t[TIMESTAMP]		The timestamp you want to discover the next transition for.
// @return TIMESTAMP		The timestamp of the next transition in trips.
TIMESTAMP evcharger_multitrip::get_next_transition(TIMESTAMP t) {
	VehicleTrip *trip;
	TIMESTAMP	next_transition;
	DATETIME	next_transition_date;
	int			i;
	double		next_transition_start_d, next_transition_offset_d, home_arrival_time_d, home_departure_time_d, destination_arrival_time_d, destination_departure_time_d;
	
	// This is definitely the next one, so lets look for the best option.
	trip = get_next_vehicle_trip(t);
	
	// Cycle through a year
	for(i = 0; i < 366; i++) {

		next_transition = t + (TIMESTAMP)(i*86400);						// Increment the next_transition TIMESTAMP by a day
		gl_localtime(next_transition,&next_transition_date);			// Update the next_transition_date struct

		// If this day is valid then we are good to go and we have a base to work from for our "next transition"
		if(vehicle_trip_valid_day_of_week(next_transition_date.weekday,trip) && vehicle_trip_valid_month_of_year(next_transition_date.month,trip)) {

			// Set the transition to the start of the day unless it is the current timestamp
			if(next_transition > t) {
				next_transition = next_transition - (int)(datetime_time_to_double_seconds(&next_transition_date));
			}
			gl_localtime(next_transition,&next_transition_date);

			next_transition_start_d 		= datetime_time_to_double_seconds(&next_transition_date); // either 0 if not day of t, or the time of day if day of t. Thus transition is next_transition + ()
			home_arrival_time_d				= double_time_to_double_seconds(trip->home_arrival_time);
			home_departure_time_d			= double_time_to_double_seconds(trip->home_departure_time);
			destination_arrival_time_d		= double_time_to_double_seconds(trip->destination_arrival_time);
			destination_departure_time_d	= double_time_to_double_seconds(trip->destination_departure_time);
			next_transition_offset_d		= 60.0*60.0*24.0; // default to twenty four hours
			
			// Always has to be after the next_transition_start_d but also 
			if(home_arrival_time_d > next_transition_start_d && (home_arrival_time_d - next_transition_start_d) < next_transition_offset_d ) {
				next_transition_offset_d = home_arrival_time_d - next_transition_start_d;
			}
			if(home_departure_time_d > next_transition_start_d && (home_departure_time_d - next_transition_start_d) < next_transition_offset_d ) {
				next_transition_offset_d = home_departure_time_d - next_transition_start_d;
			}
			if(destination_arrival_time_d > next_transition_start_d && (destination_arrival_time_d - next_transition_start_d) < next_transition_offset_d ) {
				next_transition_offset_d = destination_arrival_time_d - next_transition_start_d;
			}
			if(destination_departure_time_d > next_transition_start_d && (destination_departure_time_d - next_transition_start_d) < next_transition_offset_d ) {
				next_transition_offset_d = destination_departure_time_d - next_transition_start_d;
			}
	
			// Only break if the next transition is after the current one
			if( next_transition_offset_d < 86400.0 && next_transition_offset_d > 0 ) {
				next_transition += (TIMESTAMP)(next_transition_offset_d);
				break;
			}
		}
	}
	return next_transition;
}

// This is tricky with multiple trips for different days-of-week and months-of-year.
// @return t[TIMESTAMP]		The timestamp you want to discover the vehicle location for
// @return int				The location of the vehicle at the time t
int evcharger_multitrip::get_vehicle_location(TIMESTAMP t) {
	VehicleTrip	*trip;
	DATETIME	t_date;
	int			location = VL_UNKNOWN;		// Default to unknown
	double		t_d, t_closest_d, home_arrival_time_d, home_departure_time_d, destination_arrival_time_d, destination_departure_time_d;
	
	gl_localtime(t,&t_date);
	
	trip = get_vehicle_trip(t);
	
	// If not currently involved in a trip we are at home.
	if(trip == NULL) {
		location = VL_HOME;
	}
	// Otherwise, we need to determine which of the timeframes we are in
	else {
		t_d								= datetime_time_to_double_seconds(&t_date);							// t in double seconds for the day
		home_arrival_time_d				= double_time_to_double_seconds(trip->home_arrival_time);
		home_departure_time_d			= double_time_to_double_seconds(trip->home_departure_time);
		destination_arrival_time_d		= double_time_to_double_seconds(trip->destination_arrival_time);
		destination_departure_time_d	= double_time_to_double_seconds(trip->destination_departure_time);
		
		location = VL_HOME;				// Default to home
		
		// We need to order this so that it goes from arrival at home back to the departure from home
		if( destination_departure_time_d <= t_d && t_d < home_arrival_time_d ) {
			location = VL_DESTINATION_TO_HOME;
		} else if( destination_arrival_time_d <= t_d && t_d < destination_departure_time_d ) {
			location = VL_DESTINATION;
		} else if(home_departure_time_d <= t_d && t_d < destination_arrival_time_d ) {
			location = VL_HOME_TO_DESTINATION;
		} 

	}
	return location;
}

// Sets the charge rate for this duration
// @param t[TIMESTAMP]		The time duration until the next transition
int evcharger_multitrip::set_charge_rate(TIMESTAMP t) {
	OBJECT *obj = OBJECTHDR(this);

	switch(vehicle_data.charge_regime) {
		// Charge rate is set so that the battery is fully charged by timestamp t
		case LEAST_POWER:
			if((double)(t) > 0.0) {
				charge_rate = ( (vehicle_data.battery_size - vehicle_data.battery_capacity) * ( 1000.0 * 3600.0 ) ) / (double)(t) / vehicle_data.charge_efficiency;
				// Ensure that the charger can charge at that rate, otherwise set to maximum charge rate
				if(charge_rate > vehicle_data.max_charge_rate) {
					charge_rate = vehicle_data.max_charge_rate;
				}
			}
			// If t <= 0 and battery needs charging, set to maximum charge rate - unsure why i need this but it's here
			else if( vehicle_data.battery_soc < 100.0) {
				charge_rate = vehicle_data.max_charge_rate;
			}
			// Otherwise set the charge rate to 0
			else {
				charge_rate = 0.0;
			}
			break;
		// Charge rate is set remotely so not to be interfered with
		case REMOTE_CONTROLLED:
			break;
		// Charge rate is set to maximum charge rate - note: also default
		case FULL_POWER:
		default:
			charge_rate = vehicle_data.max_charge_rate;
			break;
	}
	
	// If the meter is off, reset to zero irrespective
	if(*pMeterStatus != 1) {
		gl_verbose("Meter is off, setting charge_rate to zero");
		charge_rate = 0;
	}
	
	gl_verbose("Time is: %d; ChargeCurrents Are: %f,%f,%f; Energy required is: %f",load_data.phaseA_I.Mag(),load_data.phaseB_I.Mag(),load_data.phaseC_I.Mag(),t,vehicle_data.battery_size - vehicle_data.battery_capacity);
	
	return 1;
}

// Sets the charger's load based on the charge_rate
// @param power[double]			Real power (W) that the charger is supplying
int evcharger_multitrip::set_charger_load(double power) {
	// Synchronise the voltage
	load_data.phaseA_V = pCircuit_V[0];
	load_data.phaseB_V = pCircuit_V[1];
	load_data.phaseC_V = pCircuit_V[2];
	gl_verbose("Voltages: %f|%f,%f|%f,%f|%f",load_data.phaseA_V.Re(),load_data.phaseA_V.Im(),load_data.phaseB_V.Re(),load_data.phaseB_V.Im(),load_data.phaseC_V.Re(),load_data.phaseC_V.Im());

	// Set load and current to zero
	load_data.phaseA_I = complex(0.0,0.0);
	load_data.phaseB_I = complex(0.0,0.0);
	load_data.phaseC_I = complex(0.0,0.0);
	load_data.phaseA_S = complex(0.0,0.0);
	load_data.phaseB_S = complex(0.0,0.0);
	load_data.phaseC_S = complex(0.0,0.0);
	
	// power is actually in W (needed for the energy side of things). Need to take into account the power factor.
	// pf_out is signed (leading or lagging) power factor
	// Real			=	power
	// Reactive		=	( power / pf_out ) * sin( acos(pf_out) )
	VA_Out = complex( power, ( power / pf_out ) * sin(acos(pf_out)) );
	gl_verbose("VA_Out: %f => %f|%f",power,VA_Out.Re(),VA_Out.Im());
	
	// Triplex-line -> Assume it's only across the 240 V for now.
	if ((phases & 0x0010) == 0x0010) {

		// If there's voltage, awesome
		if (load_data.phaseA_V.Mag() != 0.0) {
			load_data.phaseA_S = VA_Out;
			load_data.phaseA_I = ~(load_data.phaseA_S / load_data.phaseA_V);
		}
		// If not, and there's no load, awesome
		else if(VA_Out.Mag() == 0.0) {
			load_data.phaseA_S = complex(0.0,0.0);
			load_data.phaseA_I = complex(0.0,0.0);
		}
		// Otherwise something is wrong
		else {
			gl_warning("No voltage on Phase A to allow for charging"); 
		}
		
	}
	// All three phases
	else if (number_of_phases_out == 3) {
		load_data.phaseA_S = load_data.phaseB_S = load_data.phaseC_S = VA_Out/3;
		
		// Note - this does not check for voltage on the required phase. Possible problem there.
		if (load_data.phaseA_V.Mag() != 0.0)
			load_data.phaseA_I = ~(load_data.phaseA_S / load_data.phaseA_V); // /sqrt(2.0);			
		if (load_data.phaseB_V.Mag() != 0.0)
			load_data.phaseB_I = ~(load_data.phaseB_S / load_data.phaseB_V); // /sqrt(2.0);
		if (load_data.phaseC_V.Mag() != 0.0)
			load_data.phaseC_I = ~(load_data.phaseC_S / load_data.phaseC_V); // /sqrt(2.0);
		// Raise warning
		if( load_data.phaseA_V.Mag() == 0.0 || load_data.phaseB_V.Mag() == 0.0 || load_data.phaseC_V.Mag() == 0.0)
			gl_warning("One or more phases do not have a voltage. Voltage magnitudes are A:%d; B:%d; C:%d.",load_data.phaseA_V.Mag(),load_data.phaseB_V.Mag(),load_data.phaseC_V.Mag());
	}
	// Two phases
	else if (number_of_phases_out == 2) {

		// A Phase
		if ( (phases & 0x0001) == 0x0001 ) {
			if( load_data.phaseA_V.Mag() != 0 ) {
				load_data.phaseA_S = VA_Out/2;
				load_data.phaseA_I = ~(load_data.phaseA_S / load_data.phaseA_V);
			}
			else {
				gl_warning("Required A-phase does not have voltage.");
			}
		}
		
		// B Phase
		if ( (phases & 0x0002) == 0x0002 ) {
			if ( load_data.phaseB_V.Mag() != 0 ) {
				load_data.phaseB_S = VA_Out/2;
				load_data.phaseB_I = ~(load_data.phaseB_S / load_data.phaseB_V);
			}
			else {
				gl_warning("Required B-phase does not have voltage.");
			}
		}

		// C Phase
		if ( (phases & 0x0004) == 0x0004 ) {
			if ( load_data.phaseC_V.Mag() != 0 ) {
				load_data.phaseC_S = VA_Out/2;
				load_data.phaseC_I = ~(load_data.phaseC_S / load_data.phaseC_V);
			}
			else {
				gl_warning("Required C-phase does not have voltage.");
			}
		}
		
	}
	// Single phase connection
	else if (number_of_phases_out == 1) {
		if( (phases & 0x0001) == 0x0001) {
			if ( load_data.phaseA_V.Mag() != 0 ) {
				load_data.phaseA_S = VA_Out;
				load_data.phaseA_I = ~(load_data.phaseA_S / load_data.phaseA_V);
			}
			else {
				gl_warning("Required A-phase does not have voltage.");
			}
		}
		else if( (phases & 0x0002) == 0x0002 ) {
			if ( load_data.phaseB_V.Mag() != 0 ) {
				load_data.phaseB_S = VA_Out;
				load_data.phaseB_I = ~(load_data.phaseB_S / load_data.phaseB_V);
			}
			else {
				gl_warning("Required B-phase does not have voltage.");
			}
		}
		else if( (phases & 0x0004) == 0x0004 ) {
			if ( load_data.phaseC_V.Mag() != 0 ) {
				load_data.phaseC_S = VA_Out;
				load_data.phaseC_I = ~(load_data.phaseC_S / load_data.phaseC_V);
			}
			else {
				gl_warning("Required C-phase does not have voltage.");
			}
		}
	}
	else {
		gl_warning("No phases are connected or valid");
	}
	
	return 1;
}

// Charges a vehicle for a given time period at a location if it has a charger
// @param t[TIMESTAMP]		The time duration (as a TIMESTAMP) that the vehicle is charging for
// @param location[int]		The location of the vehicle being charged - used to determine if the location has a charger
// @return double			The charge_energy change of the battery_capacity (positive is charging)
double evcharger_multitrip::charge_electric_vehicle(TIMESTAMP t,int location) {
	double charge_energy = 0;
	double prev_capacity = vehicle_data.battery_capacity;
	
	if( location == VL_HOME || (location == VL_DESTINATION && vehicle_data.vehicle_trip_current->destination_has_charger) ) {
		if (enabled) {
			charge_energy = ((double)(t) / 3600.0) * (charge_rate / 1000.0) * vehicle_data.charge_efficiency;
			
			vehicle_data.battery_capacity += charge_energy;
			
			if (vehicle_data.battery_capacity < 0.0) {
				vehicle_data.battery_capacity = 0.0;
			}
			else if (vehicle_data.battery_capacity > vehicle_data.battery_size)	{
				vehicle_data.battery_capacity = vehicle_data.battery_size;
			}
		}
	}

	// Update SOC
	vehicle_data.battery_soc = vehicle_data.battery_capacity / vehicle_data.battery_size * 100.0;
	return vehicle_data.battery_capacity - prev_capacity;
}

// Sets the load.power value to the average power (kW) during the charge period.
// 	Where the time period extends over a transition (as opposed to occuring during the transition), the complete time period is used for averaging
// @param t[TIMESTAMP]		The timestamp of the next transition
// @param energy[double]	The amount of energy in kWh provided to the charger over the time period
// @return	int				0|1 based on success of update
int evcharger_multitrip::update_load_power(TIMESTAMP t, double energy) {
	if((double)(t) > 0.0) {
		// Power needs to ensure that it also takes into account the energy lost to the charger
		energy = energy / vehicle_data.charge_efficiency;
		double power = ( energy * ( 1000.0 ) ) / ( (double)(t) / ( 3600.0 ) );
		
		// First reset the charger load, given the energy actually used as the charge_rate as opposed to the 
		set_charger_load(power);
		// Add it to the parent power details
		
		// Americana Triplex-line
		if ( (phases & 0x0010) == 0x0010 ) {
			*pLine12 += load_data.phaseA_I;
		}
		// Otherwise, it's one of the others
		else {
			// gl_verbose("S.ULP.B:M.Currents: %f|%f,%f|%f,%f|%f",pLine_I[0].Re(),pLine_I[0].Im(),pLine_I[1].Re(),pLine_I[1].Im(),pLine_I[2].Re(),pLine_I[2].Im());
			pLine_I[0] += load_data.phaseA_I;
			pLine_I[1] += load_data.phaseB_I;
			pLine_I[2] += load_data.phaseC_I;
			// gl_verbose("S.ULP.A:M.Currents: %f|%f,%f|%f,%f|%f",pLine_I[0].Re(),pLine_I[0].Im(),pLine_I[1].Re(),pLine_I[1].Im(),pLine_I[2].Re(),pLine_I[2].Im());
		}
		
		return 1;
	}
	return 0;
}

// Discharges a vehicle for a given time period by driving it
// @param t[TIMESTAMP]		The time duration (as a TIMESTAMP) that the vehicle is driven
// @param location[int]		The location of the vehicle being driven
void evcharger_multitrip::drive_electric_vehicle(TIMESTAMP t,int location) {
	double driving_energy;
	
	// Calculate and remove the energy consumed to drive
	driving_energy = (vehicle_data.vehicle_trip_current->travel_distance / 2.0) / vehicle_data.mileage_efficiency;
	
	// Depends on the location as to the time it takes to go the travel_distance/2.0 however only is consumed if its in a driving "location"
	if(location == VL_HOME_TO_DESTINATION) {
		driving_energy = driving_energy * ((double)(t) / (vehicle_data.vehicle_trip_current->trip_time_to_destination));
	}
	else if (location == VL_DESTINATION_TO_HOME) {
		driving_energy = driving_energy * ((double)(t) / (vehicle_data.vehicle_trip_current->trip_time_to_home));
	}
	else {
		driving_energy = 0;
	}

	vehicle_data.battery_capacity = vehicle_data.battery_capacity - driving_energy;
	
	// Deal with edge case of battery depleted - assumed picked up
	if (vehicle_data.battery_capacity < 0.0) {
		vehicle_data.battery_capacity = 0.0;
	}
	else if (vehicle_data.battery_capacity > vehicle_data.battery_size)	{
		vehicle_data.battery_capacity = vehicle_data.battery_size;
	}

	// Update SOC
	vehicle_data.battery_soc = vehicle_data.battery_capacity / vehicle_data.battery_size * 100.0;
}

/* ------------------------------------------------------------------ 
   TIME to Hours, Seconds or Minutes for a given day helper functions
   ------------------------------------------------------------------ */

// TODO: REMOVE as unnecessary
// DRY - takes the HHMM double format of a time and converts it to a double in hours only.
// @param time[double]		The time as a double in HHMM format
// @return double			The time in hours as a double
double evcharger_multitrip::double_time_to_double_hours(double the_time) {
	double hours			= 0.0;
	double minutes			= 0.0;
	double total_hours		= 0.0;
	
	hours					= floor(the_time / 100.0);
	minutes					= fmod(the_time, 100.0);
	
	total_hours				= hours + (minutes / 60.0);
	return total_hours;
}

// TODO: REMOVE as unnecessary
// DRY - takes the HHMM double format of a time and converts it to a double in minutes only.
// @param time[double]		The time as a double in HHMM format
// @return double			The time in minutes as a double
double evcharger_multitrip::double_time_to_double_minutes(double the_time) {
	double hours			= 0.0;
	double minutes			= 0.0;
	double total_minutes	= 0.0;
	
	hours					= floor(the_time / 100.0);
	minutes					= fmod(the_time, 100.0);
	
	total_minutes			= hours * 60.0 + minutes;
	return total_minutes;
}

// DRY - takes the HHMM double format of a time and converts it to a double in seconds only.
// @param time[double]		The time as a double in HHMM format
// @return double			The time in seconds as a double
double evcharger_multitrip::double_time_to_double_seconds(double the_time) {
	double hours			= 0.0;
	double minutes			= 0.0;
	double total_seconds	= 0.0;
	
	hours					= floor(the_time / 100.0);
	minutes					= fmod(the_time, 100.0);
	
	total_seconds			= hours * 3600.0 + minutes * 60.0;
	return total_seconds;
}

// DRY - takes the HHMM double format of a time and converts it to a double in seconds only.
// @param time[double]		The time in seconds as a double
// @return double			The time as a double in HHMM format
double evcharger_multitrip::double_seconds_to_double_time(double the_time) {
	double hours			= 0.0;
	double minutes			= 0.0;
	double time_double		= 0.0;
	
	hours					= floor(the_time / 3600.0);
	minutes					= fmod(floor(the_time / 60.0),60.0);
	
	time_double				= hours * 100.0 + minutes;
	return time_double;
}

// DRY - DATETIME's time element to seconds
// @param	t[DATETIME]		The timestamp in DATETIME
// @return	double			The timestamp in seconds
double evcharger_multitrip::datetime_time_to_double_seconds(DATETIME *dt) {
	double total_seconds;
	total_seconds = ((double)(dt->hour))*3600.0 + ((double)(dt->minute))*60.0 + ((double)(dt->second));
	return total_seconds;
}

// DRY - returns a bit (2^n) verion of the integer value as an integer
// @param	i[int]			The integer
// @return	int				The integer (if i <=0 ) or the 2^n version of the integer (if i > 0)
int evcharger_multitrip::int_to_bit_version(int i) {
	int j;
	if(i > 0) {
		j = (int)(pow( 2.0, (i-1)));
		return j;
	}
	return i;
}

// Determines if a day of week is valid for a vehicle trip
// @param	day_of_week[int]	An integer of the day of week as per Gridlab DATETIME
// @return	int					0 | 1 depending on truth
int evcharger_multitrip::vehicle_trip_valid_day_of_week(int day_of_week,struct VehicleTrip *trip){
	// gl_verbose("Checking trip %d day: %d&%d",trip->trip_id,trip->days,int_to_bit_version(day_of_week+1));
	if(trip->days == 0 || trip->days & int_to_bit_version(day_of_week+1)) {
		return true;
	}
	return false;
}

// Determines if a month of year is valid for a vehicle trip
// @param	month_of_year[int]	An integer of the month_of_year as per Gridlab DATETIME
// @return	int					0 | 1 depending on truth
int evcharger_multitrip::vehicle_trip_valid_month_of_year(int month_of_year, struct VehicleTrip *trip){
	// gl_verbose("Checking trip %d month: %d&%d",trip->trip_id,trip->months,int_to_bit_version(month_of_year));
	if(trip->months == 0 || trip->months & int_to_bit_version(month_of_year)) {
		return true;
	}
	return false;
}

// Returns the integer from an object
// @param	*obj[OBJECT]		A pointer to an object
// @param	*name[char]			A pointer to a character array
// @return	int					An integer
int *evcharger_multitrip::get_enum(OBJECT *obj, char *name) {
	PROPERTY *p = gl_get_property(obj,name);
	if (p==NULL || p->ptype!=PT_enumeration)
		return NULL;
	return (int*)GETADDR(obj,p);
}

// Returns the complex from an object
// @param	*obj[OBJECT]		A pointer to an object
// @param	*name[char]			A pointer to a character array
// @return	complex				A complex
complex *evcharger_multitrip::get_complex(OBJECT *obj, char *name) {
	PROPERTY *p = gl_get_property(obj,name);
	if (p==NULL || p->ptype!=PT_complex)
		return NULL;
	return (complex*)GETADDR(obj,p);
}


/* ------------
   Debug assist
   ------------ */

// Debug assist
void evcharger_multitrip::gl_warning_vehicle_trip(struct VehicleTrip *trip) {
	if(trip != NULL) {
		gl_warning("trip_id: %d",trip->trip_id);
		gl_warning("months: %d",trip->months);
		gl_warning("days: %d",trip->days);
		gl_warning("travel_distance: %f",trip->travel_distance);
		gl_warning("trip_time_to_destination: %f",trip->trip_time_to_destination);
		gl_warning("trip_time_at_destination: %f",trip->trip_time_at_destination);
		gl_warning("trip_time_to_home: %f",trip->trip_time_to_home);
		gl_warning("trip_time_at_home: %f",trip->trip_time_at_home);
		gl_warning("home_arrival_time: %f",trip->home_arrival_time);
		gl_warning("home_departure_time: %f",trip->home_departure_time);
		gl_warning("destination_arrival_time: %f",trip->destination_arrival_time);
		gl_warning("destination_departure_time: %f",trip->destination_departure_time);	
		gl_warning("destination_has_charger: %d",trip->destination_has_charger);	
		gl_warning("*next: %d",trip->next);
		gl_warning("self: %d",trip);
	}
}


//////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION OF CORE LINKAGE
//////////////////////////////////////////////////////////////////////////

EXPORT int create_evcharger_multitrip(OBJECT **obj, OBJECT *parent)
{
	try
	{
		*obj = gl_create_object(evcharger_multitrip::oclass);

		if (*obj!=NULL)
		{
			evcharger_multitrip *my = OBJECTDATA(*obj,evcharger_multitrip);
			gl_set_parent(*obj,parent);
			my->create();
			return 1;
		}
		else
			return 0;
	}
	CREATE_CATCHALL(evcharger_multitrip);
}

EXPORT int init_evcharger_multitrip(OBJECT *obj)
{
	try {
		if (obj!=NULL) {
			evcharger_multitrip *my = OBJECTDATA(obj,evcharger_multitrip);
			return my->init(obj->parent);
		} else {
			return 0;
		}
	}
	INIT_CATCHALL(evcharger_multitrip);
}

EXPORT int isa_evcharger_multitrip(OBJECT *obj, char *classname)
{
	if(obj != 0 && classname != 0){
		return OBJECTDATA(obj,evcharger_multitrip)->isa(classname);
	} else {
		return 0;
	}
}

EXPORT TIMESTAMP sync_evcharger_multitrip(OBJECT *obj, TIMESTAMP t1, PASSCONFIG pass)
{
	TIMESTAMP t2 = TS_NEVER;
	evcharger_multitrip *my = OBJECTDATA(obj,evcharger_multitrip);
	try
	{
		switch (pass) {
		case PC_PRETOPDOWN:
			t2 = my->presync(obj->clock,t1);
			break;
		case PC_BOTTOMUP:
			t2 = my->sync(obj->clock,t1);
			break;
		case PC_POSTTOPDOWN:
			t2 = my->postsync(obj->clock,t1);
			break;
		default:
			throw "invalid pass request";
			break;
		}
		if (pass==clockpass)
			obj->clock = t1;

		return t2;
	}
	SYNC_CATCHALL(evcharger_multitrip);
}

/**@}**/
