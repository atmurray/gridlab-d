/** $Id: evse_base.cpp
	Copyright (C) 2012 Battelle Memorial Institute
	Copyright (C) 2013 Joel Courtney Ausgrid (changes - see SVN Diff)
    Copyright (C) 2017 Alan Murray
 **/
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include "evse_base.h"

// FROM INVERTER METHODS /////////////////////////////////////
static PASSCONFIG passconfig = PC_BOTTOMUP|PC_POSTTOPDOWN;
static PASSCONFIG clockpass = PC_BOTTOMUP;
////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// evse_base CLASS FUNCTIONS
//////////////////////////////////////////////////////////////////////////
CLASS* evse_base::oclass = NULL;
CLASS* evse_base::pclass = NULL;

// The EVSE
// @param		*module		MODULE
// @return		NULL
evse_base::evse_base(MODULE *module)
{
	// first time initialisation
	if (oclass==NULL)
	{
		// register the class definition
		oclass = gl_register_class(module,"evse_base",sizeof(evse_base),PC_PRETOPDOWN|PC_BOTTOMUP|PC_POSTTOPDOWN|PC_AUTOLOCK);
		if (oclass==NULL)
			GL_THROW("unable to register class evse_base");
		else
			oclass->trl = TRL_PROOF;
			
		// publish the class properties
		// Configuration for GLM file parsing, recorders, interobject interaction
		if (gl_publish_variable(oclass,
			PT_bool,		"enabled",	PADDR(enabled), PT_DESCRIPTION, "Flag to enable/disable charge point (for charging and discharging) true:enable charge point; false:disable charge point",
			
			// Battery information
			PT_double,		"charge_rate[W]", PADDR(charge_rate), PT_DESCRIPTION, "Current demanded charge rate of the vehicle",
			PT_double,		"battery_capacity[kWh]",PADDR(vehicle_data.battery_capacity), PT_DESCRIPTION, "Current capacity of the battery in kWh",
			PT_double,		"battery_soc[%]",PADDR(vehicle_data.battery_soc), PT_DESCRIPTION, "State of charge of battery",
			PT_double,		"battery_size[kWh]",PADDR(vehicle_data.battery_size), PT_DESCRIPTION, "Full capacity of battery",
			PT_double,		"maximum_charge_rate[W]",PADDR(vehicle_data.max_charge_rate), PT_DESCRIPTION, "Maximum charge rate of the charger in watts",
			PT_double,		"maximum_discharge_rate[W]",PADDR(vehicle_data.max_discharge_rate), PT_DESCRIPTION, "Maximum discharge rate of charger in watts",
			PT_double,		"charging_efficiency[unit]",PADDR(vehicle_data.charge_efficiency), PT_DESCRIPTION, "Efficiency of charger (ratio) when charging",
			
			// Charge Regime in use
			PT_enumeration,	"charge_regime", PADDR(vehicle_data.charge_regime), PT_DESCRIPTION, "Regime in use to charge the vehicle when at home",
				PT_KEYWORD,		"FULL_POWER",FULL_POWER,
				PT_KEYWORD,		"LEAST_POWER",LEAST_POWER,
				PT_KEYWORD,		"REMOTE_CONTROLLED",REMOTE_CONTROLLED,
									
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

// Creates a new evse_base instance
// @return		int			?
int evse_base::create()
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
	vehicle_data.max_charge_rate		= 10000.0;	//	Watts
	vehicle_data.max_discharge_rate		= 5000.0;	//	Watts
	vehicle_data.next_state_change		= TS_NEVER;		//	Next time the state will change
		
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
	
	// Set the default internal private variables
	prev_time						= 0;		//	Set the default
	prev_time_postsync				= 0;		//	Set this default too
	off_nominal_time				= false;	//	Assumes minimum timesteps aren't screwing things up, by default

	return 1; /* create returns 1 on success, 0 on failure */
}

// Creates a new evse_base instance
// @param		*parent[OBJECT]				Pointer to the parent object(?)
// @return		int							1 - Flag of completion
int evse_base::init(OBJECT *parent)
{
	OBJECT		*obj = OBJECTHDR(this);
	extern complex default_line_current[3];
	extern complex default_line_voltage[3];
	
	TIMESTAMP	init_time;
	DATETIME	init_date;

	// TODO: Break out parent initialisation
	// construct circuit variable map to meter
	static		complex default_line123_voltage[3], default_line1_current[3];
	static int	default_meter_status = 1;	//Not really a good place to do this, but keep consistent
	int			i;
	
	// Check for the existence of a parent
	if(parent != NULL) {
		if((parent->flags & OF_INIT) != OF_INIT){
			char objname[256];
			gl_verbose("evse_base::init(): deferring initialization on %s", gl_name(parent, objname, 255));
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
				GL_THROW("EVSE failed to map powerflow status variable");
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
						The EVSE requires that the triplex_meter contains certain published properties in order to properly connect
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
				GL_THROW("EVSE failed to map powerflow status variable");
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
			GL_THROW("EVSE must have a meter or triplex meter as it's parent");
			/*  TROUBLESHOOT
			Check the parent object of the evse_base.  The evse_base is only able to be childed via a meter or 
			triplex meter when connecting into powerflow systems.  You can also choose to have no parent, in which
			case the evcharer_multitrip will be a stand-alone application using default voltage values for solving purposes.
			*/
		}
		
	}
	// if there is no parent, run it as an isolated
	else {
		parent_string = "none";

		gl_warning("EVSE:%d has no parent meter object defined; using static voltages", obj->id);
		
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
		gl_verbose("evse_base:%s - minimum_timestep set - problems may emerge",obj->name);
		/*  TROUBLESHOOT
		The evcharger detected that the forced minimum timestep feature is enabled.  This may cause
		issues with the evcharger schedule, especially if arrival/departure times are shorter than a
		minimum timestep value.
		*/
	}
	
	//	Convert it to a timestamp value
	init_time = gl_globalclock;

	//	Extract the relevant time information
	gl_localtime(init_time,&init_date);
	// What about timezones

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
TIMESTAMP evse_base::presync(TIMESTAMP t0, TIMESTAMP t1) {
	return TS_NEVER;
}


// @param	t0[TIMESTAMP]		Current timestamp
// @param	t1[TIMESTAMP]		Previous timestamp
// @return	TIMESTAMP			
TIMESTAMP evse_base::sync(TIMESTAMP t0, TIMESTAMP t1) {
	OBJECT *obj = OBJECTHDR(this);
	double home_charge_energy;
	TIMESTAMP t2,tret, tdiff, time_to_next_state_change;
	
	gl_verbose("%s SYNC: prev_time:%d | t1:%d | t0:%d",obj->name,prev_time,t1,t0);
	
	return TS_NEVER;
}

// Postsync is called when the clock needs to advance on the second top-down pass
// @param	t0[TIMESTAMP]
// @param	t1[TIMESTAMP]
// @return	TIMESTAMP
TIMESTAMP evse_base::postsync(TIMESTAMP t0, TIMESTAMP t1) {
	OBJECT *obj = OBJECTHDR(this);
	TIMESTAMP t2 = TS_NEVER;

	gl_verbose("%s POSTSYNC: prev_time:%d | t1:%d | t0:%d",obj->name,prev_time,t1,t0);

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
int evse_base::isa(char *classname)
{
	return (strcmp(classname,"evse_base")==0);
}

// Returns the globally configured minimum timestep in use.
// @return TIMESTAMP		global_minimum_timestep
TIMESTAMP evse_base::get_global_minimum_timestep() {

	char temp_buff[128];

	//	Get global_minimum_timestep value and set the appropriate flag
	//	Retrieve the global value, only does so as a text string for some reason
	// 	So why not use a native method to get the TIMESTAMP?!?!?
	gl_global_getvar("minimum_timestep",temp_buff,sizeof(temp_buff));
	
	return (TIMESTAMP)(atof(temp_buff));
}

// Sets the charge rate for this duration
// @param t[TIMESTAMP]		The time duration until the next transition
// @return TIMESTAMP		The time duration until the next transition, battery fully charged or battery discharge
TIMESTAMP evse_base::set_charge_rate(TIMESTAMP t) {
	OBJECT *obj = OBJECTHDR(this);
	TIMESTAMP tfull = t;

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
			if( vehicle_data.battery_soc < 100.0) {
				charge_rate = vehicle_data.max_charge_rate;
			} else {
				charge_rate = 0.0;
			}
	}
	
	// If the meter is off, reset to zero irrespective
	if(*pMeterStatus != 1) {
		gl_verbose("Meter is off, setting charge_rate to zero");
		charge_rate = 0;
	}
	
	gl_verbose("Time is: %d; ChargeCurrents Are: %f,%f,%f; Energy required is: %f",load_data.phaseA_I.Mag(),load_data.phaseB_I.Mag(),load_data.phaseC_I.Mag(),t,vehicle_data.battery_size - vehicle_data.battery_capacity);
	
	if (charge_rate == 0) {
		return t;
	}
	if (charge_rate < 0) {
		// discharging		
		if (vehicle_data.battery_capacity <= 0) return t;
		tfull = vehicle_data.battery_capacity * 3600.0 / (-charge_rate * vehicle_data.charge_efficiency / 1000.0);	
	} else {
		// charging
		if (vehicle_data.battery_capacity >= vehicle_data.battery_size) return t;
		tfull = (vehicle_data.battery_size - vehicle_data.battery_capacity) * 3600.0 / (charge_rate * vehicle_data.charge_efficiency / 1000.0);	
	}
	if (tfull < 0) GL_THROW("tfull less than 0");
	if (tfull < t) return tfull;
	return t;
}

// Sets the charger's load based on the charge_rate
// @param power[double]			Real power (W) that the charger is supplying
int evse_base::set_charger_load(double power) {
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

// Charges a vehicle for a given time period
// @param t[TIMESTAMP]		The time duration (as a TIMESTAMP) that the vehicle is charging for
// @return double			The charge_energy change of the battery_capacity (positive is charging)
double evse_base::charge_electric_vehicle(TIMESTAMP t) {
	double charge_energy = 0;
	double prev_capacity = vehicle_data.battery_capacity;
	
	if (enabled) {
		charge_energy = ((double)(t) / 3600.0) * (charge_rate * vehicle_data.charge_efficiency / 1000.0 );
		
		vehicle_data.battery_capacity += charge_energy;
		
		if (vehicle_data.battery_capacity < 0.0) {
			gl_warning("Vehicle somehow over discharged, this shouldn't be possible");	
			vehicle_data.battery_capacity = 0.0;
		}
		else if (vehicle_data.battery_capacity > vehicle_data.battery_size)	{
			gl_warning("Vehicle somehow overcharged, this shouldn't be possible");
			vehicle_data.battery_capacity = vehicle_data.battery_size;
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
void evse_base::update_load_power() {
	// First reset the charger load
	set_charger_load(charge_rate);
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
}

/* ------------------------------------------------------------------ 
   TIME to Hours, Seconds or Minutes for a given day helper functions
   ------------------------------------------------------------------ */

// TODO: REMOVE as unnecessary
// DRY - takes the HHMM double format of a time and converts it to a double in hours only.
// @param time[double]		The time as a double in HHMM format
// @return double			The time in hours as a double
double evse_base::double_time_to_double_hours(double the_time) {
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
double evse_base::double_time_to_double_minutes(double the_time) {
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
double evse_base::double_time_to_double_seconds(double the_time) {
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
double evse_base::double_seconds_to_double_time(double the_time) {
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
double evse_base::datetime_time_to_double_seconds(DATETIME *dt) {
	double total_seconds;
	total_seconds = ((double)(dt->hour))*3600.0 + ((double)(dt->minute))*60.0 + ((double)(dt->second));
	return total_seconds;
}

// DRY - returns a bit (2^n) verion of the integer value as an integer
// @param	i[int]			The integer
// @return	int				The integer (if i <=0 ) or the 2^n version of the integer (if i > 0)
int evse_base::int_to_bit_version(int i) {
	int j;
	if(i > 0) {
		j = (int)(pow( 2.0, (i-1)));
		return j;
	}
	return i;
}

// Returns the integer from an object
// @param	*obj[OBJECT]		A pointer to an object
// @param	*name[char]			A pointer to a character array
// @return	int					An integer
int *evse_base::get_enum(OBJECT *obj, char *name) {
	PROPERTY *p = gl_get_property(obj,name);
	if (p==NULL || p->ptype!=PT_enumeration)
		return NULL;
	return (int*)GETADDR(obj,p);
}

// Returns the complex from an object
// @param	*obj[OBJECT]		A pointer to an object
// @param	*name[char]			A pointer to a character array
// @return	complex				A complex
complex *evse_base::get_complex(OBJECT *obj, char *name) {
	PROPERTY *p = gl_get_property(obj,name);
	if (p==NULL || p->ptype!=PT_complex)
		return NULL;
	return (complex*)GETADDR(obj,p);
}

//////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION OF CORE LINKAGE
//////////////////////////////////////////////////////////////////////////

EXPORT int create_evse_base(OBJECT **obj, OBJECT *parent)
{
	try
	{
		*obj = gl_create_object(evse_base::oclass);

		if (*obj!=NULL)
		{
			evse_base *my = OBJECTDATA(*obj,evse_base);
			gl_set_parent(*obj,parent);
			my->create();
			return 1;
		}
		else
			return 0;
	}
	CREATE_CATCHALL(evse_base);
}

EXPORT int init_evse_base(OBJECT *obj)
{
	try {
		if (obj!=NULL) {
			evse_base *my = OBJECTDATA(obj,evse_base);
			return my->init(obj->parent);
		} else {
			return 0;
		}
	}
	INIT_CATCHALL(evse_base);
}

EXPORT int isa_evse_base(OBJECT *obj, char *classname)
{
	if(obj != 0 && classname != 0){
		return OBJECTDATA(obj,evse_base)->isa(classname);
	} else {
		return 0;
	}
}

EXPORT TIMESTAMP sync_evse_base(OBJECT *obj, TIMESTAMP t1, PASSCONFIG pass)
{
	TIMESTAMP t2 = TS_NEVER;
	evse_base *my = OBJECTDATA(obj,evse_base);
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
	SYNC_CATCHALL(evse_base);
}

/**@}**/
