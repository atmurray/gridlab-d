/** $Id: evcharger_det.h
	Copyright (C) 2012 Battelle Memorial Institute
	Copyright (C) 2013 Joel Courtney Ausgrid (changes - see SVN Diff)
 **/

#ifndef _EVCHARGER_MULTITRIP_H
#define _EVCHARGER_MULTITRIP_H

//#include "gridlabd.h"
#include "energy_storage.h"
#include "power_electronics.h"

// Variables
	enum VehicleLocations {
		VL_UNKNOWN=0,								///< defines vehicle state unknown
		VL_HOME=1,									///< Vehicle is at home
		VL_DESTINATION=2,							///< Vehicle is at the destination
		VL_DESTINATION_TO_HOME=3,					///< Vehicle is commuting from the destination to home
		VL_HOME_TO_DESTINATION=4					///< Vehicle is commuting from home to the destination
	};
	// A linked list of vehicle trips.
	struct VehicleTrip {
		int			trip_id;						///< Unique identifier for the trip
		int			months;							///< Month(s) of operation for this trip (bitwise addition and zero as all):	1:jan, 2:feb, 4:mar, 8:apr, 16:may, 32:jun, 64:jul, 128:aug, 256:sep, 512:oct, 1024:nov, 2048:dec
		int			days;							///< Day(s) of operation for this trip (bitwise addition and zero as all): 		1:sun, 2:mon, 4:tue, 8:wed, 16:thu, 32:fri, 64:sat
		double		travel_distance;				///< Round trip distance the vehicle travels on trip from (ie home-to-dest-dist = travel_distance/2)
		double		trip_time_to_destination;		///< Time taken to get to the destination (s)
		double		trip_time_at_destination;		///< Time waiting at destination (s)
		double		trip_time_to_home;				///< Time taken to get home - should be equal to trip_time_to_destination in most cases (s)
		double		trip_time_at_home;				///< Time waiting at home (s)
		double		home_departure_time;			///< Clock time depart home (HHMM)
		double		home_arrival_time;				///< Clock time arrive at home (HHMM)
		double		destination_departure_time;		///< Clock time depart destination (HHMM)
		double		destination_arrival_time;		///< Clock time arrive at destination (HHMM)
		int			destination_has_charger;		///< Whether or not the destination has a charger
		struct 		VehicleTrip *next;				///< Next trip in double linked list
	};

	struct VehicleData  {
		enumeration		location;					///< Vehicle current locational state
		enumeration		charge_regime;				///< Charge regime the vehicle is under control of
		double			battery_capacity;			///< Current capacity of the battery (kW-hours)
		double			battery_soc;				///< Battery state of charge
		double			battery_size;				///< Maximum battery size
		double			max_charge_rate;			///< Maximum rate of charge for the charger (Watts)
		double			max_discharge_rate;			///< Maximum rate of discharge for the charger (Watts)
		double			charge_efficiency;			///< Efficiency of the charger (power in to battery power)
		double			mileage_efficiency;			///< Powertrain battery use - miles/kWh
		TIMESTAMP		next_state_change;			///< Timestamp of next transition (home->work, work->home)

		// VehicleTrip references
		struct VehicleTrip	*vehicle_trip_start;	///< Pointer to the first VehicleTrip		- NULL for none
		struct VehicleTrip	*vehicle_trip_end;		//< Pointer to the last VehicleTrip		- NULL for none
		struct VehicleTrip	*vehicle_trip_current;	///< Pointer to the current vehicle trip	- NULL for none
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

class evcharger_multitrip : public power_electronics
{
	/* ---------
		GRIDLAB
	   --------- */
	public:
		//	Variables
		static CLASS *oclass, *pclass;
		
		bool	default_NR_mode;								///< False if not otherwise set
		

		//	Functions
		evcharger_multitrip(MODULE *module);
		~evcharger_multitrip();
		int					create();
		int					init(OBJECT *parent);
		int					isa(char *classname);		
		TIMESTAMP			presync(TIMESTAMP t0, TIMESTAMP t1);
		TIMESTAMP 			sync(TIMESTAMP t0, TIMESTAMP t1);
		TIMESTAMP			postsync(TIMESTAMP t0, TIMESTAMP t1);

	private:
		//	Variables
		TIMESTAMP glob_min_timestep;							///< Variable for storing minimum timestep value - if it exists 
		bool off_nominal_time;									///< Flag to indicate a minimum timestep is present		
		TIMESTAMP prev_time;									///< Tracking variable
		TIMESTAMP prev_time_postsync;							///< Tracking variable
		//	Functions
		TIMESTAMP			get_global_minimum_timestep();		///< Helper function to get the global_minimum_timestep
		
	
	/* ---------------------------
	   Electric Vehicle Multitrip
	   --------------------------- */
	public:
		// Variables

		
		// Vehicle information
		char1024		vehicle_name;				///< Name of the element
		VehicleData 	vehicle_data;				///< Structure of information for this particular PHEV/EV
		// VehicleTrips	vehicle_trips;				///< Structure of the trips
		char1024		vehicle_trips_indexes_str;	///< Indexes of trips in the CSV to apply to this EV in order of index
		char1024		vehicle_trips_csv_file;		///< CSV File of the trip details with structure a minimum data set and headers of:
													///< trip_id,season,days,travel_distance,trip_time_to_destination,trip_time_pause,trip_time_to_home,home_departure_time,home_arrival_time,destination_departure_time,destination_arrival_time,destination_has_charger
													///< note: header required to 
		int				trip_id;					///< Recorder holder of the current trip
		char1024		current_vehicle_trip_str;	///< String of current vehicle trip information

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

	private:		
		//	Variables
		
		//	Functions to get information from parent object
		//bool 				*get_bool(OBJECT *obj, char *name);					///< 
		int					*get_enum(OBJECT *obj, char *name);					///< 
		complex				*get_complex(OBJECT *obj, char *name);				///< 

		// Trip dependent
		int					init_vehicle_trips();								///< Initialise the vehicle_trips
		int					complete_trip_details(struct VehicleTrip *trip);	///< Updates a trip's details where they are blank
		int					vehicle_trips_length();								///< Length of the linked list of vehicle_trips
		int					get_vehicle_location(TIMESTAMP t);					///< The location of a vehicle at a given timestamp
		struct VehicleTrip	*get_vehicle_trip(TIMESTAMP t);						///< Get a vehicle_trip for a given TIMESTAMP, returning NULL if all are at home
		struct VehicleTrip	*get_next_vehicle_trip(TIMESTAMP t);				///< Get the next valid vehicle_trip for a given TIMESTAMP
		TIMESTAMP			get_next_transition(TIMESTAMP t);					///< The timestamp of the next transition of vehicle location
		
		// Validate the vehicle trip against the day of week or month of year
		int					vehicle_trip_valid_day_of_week(int day_of_week,struct VehicleTrip *trip);
		int					vehicle_trip_valid_month_of_year(int month_of_year, struct VehicleTrip *trip);

		// Alter the electric vehicle's state of charge
		int					set_charge_rate(TIMESTAMP t);						///< Sets the charge_rate based on the various types of 
		int					set_charger_load(double power);						///< Sets the charge_rate based on the various types of 
		int					update_load_power(TIMESTAMP t, double energy);		///< Updates the load power based on the difference in energy before + after charge and the duration TIMESTAMP t
		double				charge_electric_vehicle(TIMESTAMP t,int location);	///< Charge the electric vehicle for a duration of TIMESTAMP t at location VL_HOME or VL_DESTINATION
		void				drive_electric_vehicle(TIMESTAMP t,int location);	///< Drive to a destination for duration TIMESTAMP t to a location
		
		// Time helpers - doing it DRY!
		// TODO: migrate time as double to time as short
		double				double_time_to_double_hours(double the_time);		///< Convert HHMM to a double of hours
		double				double_time_to_double_minutes(double the_time);		///< Convert HHMM to a double of minutes
		double				double_time_to_double_seconds(double the_time);		///< Convert HHMM to a double of seconds
		double				double_seconds_to_double_time(double the_time);		///< Convert double of seconds to HHMM
		double				datetime_time_to_double_seconds(DATETIME *dt);		///< Convert DATETIME to a double of seconds

		// Parsing helper 
		int					int_to_bit_version(int i);
		
		// Warning & Debug helpers for structs
		void				gl_warning_vehicle_trip(struct VehicleTrip *trip);
};

#endif // _EVCHARGER_DET_TRIPS_H

/**@}**/
