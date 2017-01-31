/** $Id: evse_event.h
	Copyright (C) 2017 Alan Murray
 **/

#ifndef _EVSE_EVENT_H
#define _EVSE_EVENT_H

#include "evse_base.h"

// A linked list of vehicle trips.
struct ChargeEvent {
	int			event_id;						///< Unique identifier for the trip
	int			months;							///< Month(s) of operation for this trip (bitwise addition and zero as all):	1:jan, 2:feb, 4:mar, 8:apr, 16:may, 32:jun, 64:jul, 128:aug, 256:sep, 512:oct, 1024:nov, 2048:dec
	int			days;							///< Day(s) of operation for this trip (bitwise addition and zero as all): 		1:sun, 2:mon, 4:tue, 8:wed, 16:thu, 32:fri, 64:sat
	double		arrival_time;					///< Clock time arrive at home (HHMM)
	double		arrival_soc;					///< Battery state of charge on arrival (percentage)
	double		departure_time;					///< Clock time depart home (HHMM)
	double		requested_soc;					///< Requested state of charge on departure (percentage)
	double		battery_size;					///< Battery capacity (kW-h)
	struct 		ChargeEvent *next;				///< Next trip in double linked list
};

class evse_event : public evse_base
{
	/* ---------
		GRIDLAB
	   --------- */
	public:
		//	Variables
		static CLASS *oclass, *pclass;
		
		bool	default_NR_mode;								///< False if not otherwise set
		
		//	Functions
		evse_event(MODULE *module);
		~evse_event();
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

		char1024		charge_events_indexes_str;	///< Indexes of trips in the CSV to apply to this EV in order of index
		char1024		charge_events_csv_file;		///< CSV File of the trip details with structure a minimum data set and headers of:
													///< event_id,season,days,travel_distance,trip_time_to_destination,trip_time_pause,trip_time_to_home,departure_time,arrival_time,destination_departure_time,destination_arrival_time,destination_has_charger
													///< note: header required to 
		int				event_id;					///< Recorder holder of the current trip
		char1024		current_charge_event_str;	///< String of current vehicle trip information

		/// Vehicle trip information
		enumeration		location;					///< Vehicle current locational state
		struct ChargeEvent	*charge_event_start;	///< Pointer to the first ChargeEvent		- NULL for none
		struct ChargeEvent	*charge_event_end;		//< Pointer to the last ChargeEvent		- NULL for none
		struct ChargeEvent	*charge_event_current;	///< Pointer to the current vehicle trip	- NULL for none		

	private:		
		// Trip dependent
		int					init_charge_events();								///< Initialise the charge_events
		int					complete_event_details(struct ChargeEvent *trip);	///< Updates a trip's details where they are blank
		int					charge_events_length();								///< Length of the linked list of charge_events
		int					get_vehicle_location(TIMESTAMP t);					///< The location of a vehicle at a given timestamp
		struct ChargeEvent	*get_charge_event(TIMESTAMP t);						///< Get a charge_event for a given TIMESTAMP, returning NULL if all are at home
		struct ChargeEvent	*get_next_charge_event(TIMESTAMP t);				///< Get the next valid charge_event for a given TIMESTAMP
		TIMESTAMP			get_next_transition(TIMESTAMP t);					///< The timestamp of the next transition of vehicle location
		
		// Validate the vehicle trip against the day of week or month of year
		int					charge_event_valid_day_of_week(int day_of_week,struct ChargeEvent *trip);
		int					charge_event_valid_month_of_year(int month_of_year, struct ChargeEvent *trip);
				
		// Warning & Debug helpers for structs
		void				gl_warning_charge_event(struct ChargeEvent *trip);
};

#endif // _EVCHARGER_DET_TRIPS_H

/**@}**/
