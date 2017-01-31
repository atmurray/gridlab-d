/** $Id: evse_base.h
	Copyright (C) 2012 Battelle Memorial Institute
	Copyright (C) 2013 Joel Courtney Ausgrid (changes - see SVN Diff)
	Copyright (C) 2017 Alan Murray
 **/

#ifndef _EVSE_BASE_H
#define _EVSE_BASE_H

#include "energy_storage.h"
#include "power_electronics.h"

struct VehicleData  {
	enumeration		charge_regime;				///< Charge regime the EVSE is under control of
	double			battery_capacity;			///< Current capacity of the battery (kW-hours)
	double			battery_soc;				///< Battery state of charge
	double			battery_size;				///< Maximum battery size
	double			max_charge_rate;			///< Maximum rate of charge for the charger (Watts)
	double			max_discharge_rate;			///< Maximum rate of discharge for the charger (Watts)
	double			charge_efficiency;			///< Efficiency of the charger (power in to battery power)
	TIMESTAMP		next_state_change;			///< Timestamp of next transition (home->work, work->home)
};

enum EVChargeRegimes {
	FULL_POWER=1,							///< Battery charges at the maximum charge rate
	LEAST_POWER=2,							///< Battery charges at the minimum charge rate to fill battery in the available time
	REMOTE_CONTROLLED=3						///< Battery charge rate is at the control of an external object
};

// Load data views positive load as load; negative load as generation.
struct LoadData {
	complex phaseA_V;			///< Phase A Voltage
	complex phaseB_V;			///< Phase B Voltage
	complex phaseC_V;			///< Phase C Voltage
	complex phaseA_I;			///< Phase A Current
	complex phaseB_I;			///< Phase B Current
	complex phaseC_I;			///< Phase C Current
	complex phaseA_S;			///< Phase A Power
	complex phaseB_S;			///< Phase B Power
	complex phaseC_S;			///< Phase C Power
};

class evse_base : public power_electronics
{
	/* ---------
		GRIDLAB
	   --------- */
	public:
		//	Variables
		static CLASS *oclass, *pclass;
		
		bool	default_NR_mode;								///< False if not otherwise set
		
		//	Functions
		evse_base(MODULE *module);
		~evse_base() {};
		int					create();
		int					init(OBJECT *parent);
		int					isa(char *classname);		
		TIMESTAMP			presync(TIMESTAMP t0, TIMESTAMP t1);
		TIMESTAMP 			sync(TIMESTAMP t0, TIMESTAMP t1);
		TIMESTAMP			postsync(TIMESTAMP t0, TIMESTAMP t1);

	protected:
		//	Variables
		TIMESTAMP glob_min_timestep;							///< Variable for storing minimum timestep value - if it exists 
		bool off_nominal_time;									///< Flag to indicate a minimum timestep is present		
		TIMESTAMP prev_time;									///< Tracking variable
		TIMESTAMP prev_time_postsync;							///< Tracking variable
		//	Functions
		TIMESTAMP			get_global_minimum_timestep();		///< Helper function to get the global_minimum_timestep	
	
	/* ---------------------------
	   Electric Vehicle Base
	   --------------------------- */
	public:
		// Vehicle information
		char1024		vehicle_name;				///< Name of the element
		VehicleData 	vehicle_data;				///< Structure of information for this particular PHEV/EV

		bool enabled;								///< Enables or disables the charge point

		// Meter information
		double	charge_rate;						///< Charge rate being used (in Amps)
		complex *pCircuit_V;						///< pointer to the parent three voltages on three lines
		complex *pLine_I;							///< pointer to the parent three current on three lines
		complex *pPower;							///< pointer to the parent three power loads on three lines
		complex *pLine12;							///< pointer to the parent line current used in triplex metering
		complex *pPower12;							///< pointer to the parent line power used in triplex metering
		int *pMeterStatus;							///< Pointer to the parent service_status variable on parent
		complex VA_Out;								///< Calculation of power based on pf_out & charge_rate (modified for limits)
		
		// Charger Load Information
		struct LoadData	load_data;					///< Struct of three phase LoadData holding Voltage, Current and Power information for the current charger load

	protected:		
		//	Variables
		
		//	Functions to get information from parent object
		//bool 				*get_bool(OBJECT *obj, char *name);					///< 
		int					*get_enum(OBJECT *obj, char *name);					///< 
		complex				*get_complex(OBJECT *obj, char *name);				///< 

		// Alter the electric vehicle's state of charge
		TIMESTAMP			set_charge_rate(TIMESTAMP t);						///< Sets the charge_rate based on the various types of 
		int					set_charger_load(double power);						///< Sets the charge_rate based on the various types of 
		void				update_load_power();								///< Updates the load power based on the set charge_rate
		double				charge_electric_vehicle(TIMESTAMP t);				///< Charge the electric vehicle for a duration of TIMESTAMP t
		
		// Time helpers
		// TODO: migrate time as double to time as short
		double				double_time_to_double_hours(double the_time);		///< Convert HHMM to a double of hours
		double				double_time_to_double_minutes(double the_time);		///< Convert HHMM to a double of minutes
		double				double_time_to_double_seconds(double the_time);		///< Convert HHMM to a double of seconds
		double				double_seconds_to_double_time(double the_time);		///< Convert double of seconds to HHMM
		double				datetime_time_to_double_seconds(DATETIME *dt);		///< Convert DATETIME to a double of seconds

		// Parsing helper 
		int					int_to_bit_version(int i);		
};

#endif // _EVCHARGER_DET_TRIPS_H

/**@}**/
