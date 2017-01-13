/** $Id: evse_base.h
	Copyright (C) 2012 Battelle Memorial Institute
	Copyright (C) 2013 Joel Courtney Ausgrid (changes - see SVN Diff)
	Copyright (C) 2017 Alan Murray
 **/

#ifndef _EVSE_ITRIP_H
#define _EVSE_ITRIP_H

#include "evse_base.h"

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
	double		mileage_efficiency;			///< Powertrain battery use - miles/kWh	
	struct 		VehicleTrip *next;				///< Next trip in double linked list
};

// Variables
enum VehicleLocations {
	VL_UNKNOWN=0,								///< defines vehicle state unknown
	VL_HOME=1,									///< Vehicle is at home
	VL_DESTINATION=2,							///< Vehicle is at the destination
	VL_DESTINATION_TO_HOME=3,					///< Vehicle is commuting from the destination to home
	VL_HOME_TO_DESTINATION=4					///< Vehicle is commuting from home to the destination
};

class evse_trip : public evse_base
{
	/* ---------
		GRIDLAB
	   --------- */
	public:
		//	Variables
		static CLASS *oclass, *pclass;
		
		bool	default_NR_mode;								///< False if not otherwise set
		
		//	Functions
		evse_trip(MODULE *module);
		~evse_trip();
		int					create();
		int					init(OBJECT *parent);
		int					isa(char *classname);		
		TIMESTAMP 			sync(TIMESTAMP t0, TIMESTAMP t1);

	private:
		//	Variables
		TIMESTAMP glob_min_timestep;							///< Variable for storing minimum timestep value - if it exists 
		bool off_nominal_time;									///< Flag to indicate a minimum timestep is present		
		TIMESTAMP prev_time;									///< Tracking variable
		TIMESTAMP prev_time_postsync;							///< Tracking variable
	
	/* ---------------------------
	   Trip based EVSE charging
	   --------------------------- */
	public:
		// Variables

		char1024		vehicle_trips_indexes_str;	///< Indexes of trips in the CSV to apply to this EV in order of index
		char1024		vehicle_trips_csv_file;		///< CSV File of the trip details with structure a minimum data set and headers of:
													///< trip_id,season,days,travel_distance,trip_time_to_destination,trip_time_pause,trip_time_to_home,home_departure_time,home_arrival_time,destination_departure_time,destination_arrival_time,destination_has_charger
													///< note: header required to 
		int				trip_id;					///< Recorder holder of the current trip
		char1024		current_vehicle_trip_str;	///< String of current vehicle trip information

		/// Vehicle trip information
		enumeration		location;					///< Vehicle current locational state
		struct VehicleTrip	*vehicle_trip_start;	///< Pointer to the first VehicleTrip		- NULL for none
		struct VehicleTrip	*vehicle_trip_end;		//< Pointer to the last VehicleTrip		- NULL for none
		struct VehicleTrip	*vehicle_trip_current;	///< Pointer to the current vehicle trip	- NULL for none		

	private:		
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
		void				drive_electric_vehicle(TIMESTAMP t,int location);	///< Drive to a destination for duration TIMESTAMP t to a location
				
		// Warning & Debug helpers for structs
		void				gl_warning_vehicle_trip(struct VehicleTrip *trip);
};

#endif // _EVCHARGER_DET_TRIPS_H

/**@}**/
