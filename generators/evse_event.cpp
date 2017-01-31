/** $Id: evse_event.cpp
	Copyright (C) 2012 Battelle Memorial Institute
	Copyright (C) 2013 Joel Courtney Ausgrid (changes - see SVN Diff)
    Copyright (C) 2017 Alan Murray
 **/
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <math.h>
#include "evse_event.h"

// FROM INVERTER METHODS /////////////////////////////////////
static PASSCONFIG passconfig = PC_BOTTOMUP|PC_POSTTOPDOWN;
static PASSCONFIG clockpass = PC_BOTTOMUP;
////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////
// evse_event CLASS FUNCTIONS
//////////////////////////////////////////////////////////////////////////
CLASS* evse_event::oclass = NULL;
CLASS* evse_event::pclass = NULL;

// The evse_event
// @param		*module		MODULE
// @return		NULL
evse_event::evse_event(MODULE *module) : evse_base(module)
{
	// first time initialisation
	if (oclass==NULL)
	{
		// register the class definition
		oclass = gl_register_class(module,"evse_event",sizeof(evse_event),PC_PRETOPDOWN|PC_BOTTOMUP|PC_POSTTOPDOWN|PC_AUTOLOCK);
		if (oclass==NULL)
			throw "unable to register class evse_event";
		else
			oclass->trl = TRL_PROOF;
			
		// publish the class properties
		// Configuration for GLM file parsing, recorders, interobject interaction
		if (gl_publish_variable(oclass,
			PT_INHERIT,		"evse_base",
			
			// Charge Event Files
			PT_char1024,	"charge_events_csv_file", PADDR(charge_events_csv_file), PT_DESCRIPTION, "Path to .CSV file with charge event data",
			PT_char1024,	"charge_events_indexes", PADDR(charge_events_indexes_str), PT_DESCRIPTION, "A single line CSV string of the indexes of charge events for this EVSE",
			
			// Current Status
			PT_char1024,	"current_charge_event", PADDR(current_charge_event_str), PT_DESCRIPTION, "Details of the current charge event",
			PT_int32,		"event_id",PADDR(event_id), PT_DESCRIPTION, "ID of current active charge event or 0 for no charge event",
			
			NULL)<1) 
			GL_THROW("unable to publish properties in %s",__FILE__);
	}
}

// Creates a new evse_event instance
// @return		int			?
int evse_event::create()
{
	evse_base::create();
	
	// Default to no charge event
	charge_event_start = NULL;
	charge_event_end = NULL;
	charge_event_current = NULL;
		
	// Default to no charge event
	event_id = 0;
	
	// Event data
	charge_events_csv_file[0] = '\0';
	charge_events_indexes_str[0] = '\0'; //	Null for the vehicle string too
	
	return 1; /* create returns 1 on success, 0 on failure */
}

// Creates a new evse_event instance
// @param		*parent[OBJECT]				Pointer to the parent object(?)
// @return		int							1 - Flag of completion
int evse_event::init(OBJECT *parent)
{
	OBJECT		*obj = OBJECTHDR(this);
	TIMESTAMP	init_time;
	DATETIME	init_date;

	evse_base::init(parent);

	ChargeEvent	*temp_charge_event;
	
	// Event CSV support
	if (charge_events_csv_file[0] != '\0' && charge_events_indexes_str[0] != '\0') {
		if(init_charge_events()) {
			// 
			gl_verbose("Imported %d charge events",charge_events_length());
		}
		else {
			// 
			gl_warning("Could not import events");
		}
	} else if (charge_event_start == NULL) {
		gl_warning("No charge event information.");
		if (charge_events_csv_file[0] == '\0') {
			gl_warning("No events available.");
		} else if (charge_events_indexes_str[0] != '\0') {
			gl_warning("No events selected.");
		}
	}

	//	Convert it to a timestamp value
	init_time = gl_globalclock;

	//	Extract the relevant time information
	gl_localtime(init_time,&init_date);
	// What about timezones

	// Get the charge_event that is applicable at the moment
	charge_event_current = get_charge_event(init_time);
	if(charge_event_current == NULL) {
		event_id = 0;
		charge_rate = 0;
		vehicle_data.battery_size = 0.0;
		vehicle_data.battery_soc = 0.0;
		vehicle_data.battery_capacity = 0.0;
	} else {
		event_id = charge_event_current->event_id;
		vehicle_data.battery_size = charge_event_current->battery_size;		
		vehicle_data.battery_soc = charge_event_current->arrival_soc;
		vehicle_data.battery_capacity = charge_event_current->battery_size * charge_event_current->arrival_soc;
	}

	// Set the starting location and next transition timestamp
	vehicle_data.next_state_change = get_next_transition(init_time);	
	
	// Set the charge rate that we are starting at
	set_charge_rate(vehicle_data.next_state_change - init_time);
	// Sets the charger load, using the current charge_rate (W)
	// TODO: We need to have this to make it work, so we'll call update_load_power with a time
	// 			of 1s and energy of (charge_rate / 1000.0) * (1 / 3600.0)
	// update_load_power(1, (charge_rate / 1000.0) * (1 / 3600.0) );

	// Return 1
	return 1;
}

// @param	t0[TIMESTAMP]		Previous timestamp
// @param	t1[TIMESTAMP]		Current timestamp
// @return	TIMESTAMP			
TIMESTAMP evse_event::sync(TIMESTAMP t0, TIMESTAMP t1) {
	OBJECT *obj = OBJECTHDR(this);
	double charge_energy;
	TIMESTAMP t2, tdiff, tnext;
	ChargeEvent	*next_charge_event;
	
	// Time step currently in play
	tdiff = t1 - t0;

	gl_warning("%s t0: %d t1: %d ", obj->name, t0, t1);

	// If we had an active charge event, charge the attached vehicle
	if (event_id != 0) {
		// charge the vehicle based on previous charge rate
		charge_energy = charge_electric_vehicle(tdiff);
		gl_warning("%s charge_energy is %f for tdiff %d", obj->name, charge_energy, tdiff);
	}
	
	// Update the current connected vehicle
	next_charge_event = get_charge_event(t1);
		
	if (next_charge_event == NULL) {
		if ((charge_event_current != NULL) && (charge_rate != 0)) {
			// This little hack ensures that it is possible to record the final charge state of a vehicle when it leaves
			gl_warning("%s finished event %d with SOC of %f and charge_rate %f", obj->name, event_id, vehicle_data.battery_soc, charge_rate);
			charge_event_current = NULL;
			vehicle_data.next_state_change = t1 + 1;
	
			// Update the load power
			update_load_power();

			return vehicle_data.next_state_change;
		} else {
			// no active charge event
			gl_warning("No charge event");
			charge_event_current = NULL;			
			event_id = 0;
			charge_rate = 0;
			vehicle_data.battery_size = 0.0;
			vehicle_data.battery_soc = 0.0;
			vehicle_data.battery_capacity = 0.0;
		}
	} else {
		// active charge event
		if (charge_event_current == NULL) {
			gl_warning("%s starting event %d with SOC of %f", obj->name, event_id, vehicle_data.battery_soc);
			charge_event_current = next_charge_event;
			event_id = charge_event_current->event_id;
			vehicle_data.battery_size = charge_event_current->battery_size;		
			vehicle_data.battery_soc = charge_event_current->arrival_soc;
			vehicle_data.battery_capacity = charge_event_current->battery_size * charge_event_current->arrival_soc;
		} else if (charge_event_current != next_charge_event) {
			gl_warning("%s charge event %d interrupted charge event %d with SOC of %f with requested departure SOC of %f", obj->name, charge_event_current->event_id, event_id, vehicle_data.battery_soc, charge_event_current->requested_soc);		
			charge_event_current = next_charge_event;
			event_id = charge_event_current->event_id;
			vehicle_data.battery_size = charge_event_current->battery_size;		
			vehicle_data.battery_soc = charge_event_current->arrival_soc;
			vehicle_data.battery_capacity = charge_event_current->battery_size * charge_event_current->arrival_soc;
		} else {
			gl_warning("%s continuing charge event %d", obj->name, charge_event_current->event_id);
		}
	}

	// work out when the next transition is
	vehicle_data.next_state_change = get_next_transition(t1);

	gl_verbose("%s SYNC (%d): prev_time:%d | t1:%d | t0:%d | tnext: %d", obj->name, event_id, prev_time, t1, t0, vehicle_data.next_state_change);
	
	// Only perform an action if the time has changed and isn't first time
	// This is due to the impact on the energy in the battery of the EV.
	if ((prev_time != t1) && (t0 != 0)) {		

		// Time step to the state change for the electric vehicle
		tnext = vehicle_data.next_state_change - t1;
		gl_verbose("Time to next SC: %d", tnext);

		// If we have an active charge event, charge the attached vehicle and update charge rate
		if (event_id != 0) {
			// update the charge rate
			vehicle_data.next_state_change = t1 + set_charge_rate(tnext);
		}
		
		// Update the load power
		update_load_power();

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

	// Minimum timestep check
	if (off_nominal_time == true)
	{
		// See where our next "expected" time is
		t2 = t1 + glob_min_timestep;
		if (vehicle_data.next_state_change < t2)	// tret is less than the next "expected" timestep
			vehicle_data.next_state_change = t2;	// Unfortunately, GridLAB-D is "special" and doesn't know how to handle this with min timesteps.  We have to fix it.
	}
	gl_warning("%s charge event %d, next transition is %d", obj->name, event_id, vehicle_data.next_state_change);			

	return vehicle_data.next_state_change;
}

// @return int				0 or 1 based on if the object is of type classname
int evse_event::isa(char *classname)
{
	return (strcmp(classname,"evse_event")==0 || evse_base::isa(classname)) ;
}

// Initialises the charge events
// TODO: Tidy up function variables
// @return int				true or false based on getting the data
int evse_event::init_charge_events() {

	struct ColumnList {
		char	name[128];
		struct	ColumnList *next;
	};													///< ColumnList structure

	ChargeEvent			*temp_charge_event;				///< Temp element for Vehicle event to add to the charge_events
	FILE				*charge_event_csv_file_handler;	///< File handler for the charge_event_csv file
	struct ColumnList	*column_first = 0, *column_last = 0, *column_current = 0;
														///< ColumnList pointers
	
	char				line[1024];						///< Line of CSV
	char				buffer[1024];					///< The buffer for manipulation of a line
	char				buffer2[128];					///< The buffer for manipulation of a line
	char				*token = 0;						///< Token for data
	char				*next;							///< state for strok_s
	const char			delim[] = ",\n\r\t";			///< delimiters to tokenise
	
	int					has_cols	= 0;				///< Boolean to flag the columns of the CSV as obtained and populated into the column_list
	int					event_id		= 0;				///< event_id integer
	int					column_count = 0;				///< Total count of columns
	int					col = 0;						///< Current column counter for looping through all columns
	int					row = 0;

	// First we need to create the charge_events linked list based on the indexes provided. Then we go through and populate them
	memset(buffer, 0, 1024);							///< This overwrites the buffer with null values
	strncpy(buffer, charge_events_indexes_str, 1024);	///< Copies the charge_event_csv_file_handler into the buffer for use

	token = strtok_s(buffer, delim, &next);			///< Now use the buffer to break up by commas, turn into integers, and then add to the list.
	// Move through each
	while( token != NULL ) {
		memset(buffer2, 0, 128);
		strcpy(buffer2,token);

		event_id = (int)(atol(buffer2));					///< Grab the index

		// Always work with the last element
		// No charge_events
		if(charge_event_start == 0) {
			charge_event_start		= new ChargeEvent;
			charge_event_end		= charge_event_start;
			charge_event_end->next	= charge_event_start;
		}
		// charge events exist, so create a new one
		else {
			charge_event_end->next = new ChargeEvent;
			charge_event_end		= charge_event_end->next;
			charge_event_end->next = charge_event_start;
		}
		// Set all default values
		// Use negatives for doubles that would otherwise be NULL
		charge_event_end->event_id						= event_id;
		charge_event_end->months						= 0;
		charge_event_end->days							= 0;
		charge_event_end->arrival_time					= -1;
		charge_event_end->arrival_soc					= -1;
		charge_event_end->departure_time				= -1;
		charge_event_end->requested_soc					= -1;
		charge_event_end->battery_size					= -1;

		token = strtok_s(NULL, delim, &next);
	}
	temp_charge_event = charge_event_start;

	// Open the file
	charge_event_csv_file_handler = fopen(charge_events_csv_file, "r");
	// If invalid return 0 and a warning
	if(charge_event_csv_file_handler == 0){
		GL_THROW("Could not open \'%s\' (\'charge_events_csv_file\') for input!\n\tCheck for Windows or Linux paths.\n\tUse double quotes.", (char*)charge_events_csv_file);
		/* TROUBLESHOOT
			The specified input file could not be opened for reading.  Verify that no
			other applications are using that file, double-check the input model, and
			re-run GridLAB-D.
		*/
		return 0;
	}
	// Otherwise do the reading and creating of charge events!
	else {
		// Go through each line
		while(fgets(line, 1024, charge_event_csv_file_handler) > 0){

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
				// First token is the event_id
				else {
					col = 0;
					column_current = column_first;
				
					event_id = (int)(atol(token));
				
					// If the event is the one we are looking for then awesome
					if(temp_charge_event->event_id == event_id) {
						
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
								temp_charge_event->months = (int)(atof(buffer2));
							} else if(strcmp(column_current->name,"days") == 0) {
								temp_charge_event->days = (int)(atof(buffer2));
							} else if(strcmp(column_current->name,"arrival_time") == 0) {
								temp_charge_event->arrival_time = atof(buffer2);
								// Deal with decimal based HHMM format
								if(temp_charge_event->arrival_time >= 1 ) {
									temp_charge_event->arrival_time = double_time_to_double_seconds(temp_charge_event->arrival_time) / 86400;
								}
							} else if(strcmp(column_current->name,"arrival_soc") == 0) {
								temp_charge_event->arrival_soc = atof(buffer2);
							} else if(strcmp(column_current->name,"departure_time") == 0) {
								temp_charge_event->departure_time = atof(buffer2);
								// Deal with decimal based HHMM format
								if(temp_charge_event->departure_time >= 1 ) {
									temp_charge_event->departure_time = double_time_to_double_seconds(temp_charge_event->departure_time) / 86400;
								}
							} else if(strcmp(column_current->name,"requested_soc") == 0) {
								temp_charge_event->requested_soc = atof(buffer2);
							} else if(strcmp(column_current->name,"battery_size") == 0) {
								temp_charge_event->battery_size = atof(buffer2);
							}

							// Go to the next token
							token = strtok_s(NULL, delim, &next);
							col++;
							column_current = column_current->next;
						}
					
						// TODO: move to vaidation step in the ev_charger::init function
						complete_event_details(temp_charge_event);
						
						// Now we need to deal with the next one
						temp_charge_event = temp_charge_event->next;
					}
				}
			}
		}
	
	}
	// Close the file
	fclose(charge_event_csv_file_handler);
	
	// All good so return 1
	return 1;
}

// The ChargeEvent linked list is kept with the end element referencing the start element and these
// 		are held in cache.
// @return int				number of ChargeEvents
int evse_event::charge_events_length() {
	ChargeEvent	*event;
	int			i;

	event = charge_event_start;
	i = 0;
	if(event != NULL) {
		do {
			i++;
			event = event->next;
		} while(event != charge_event_start);
	}
	
	return i;
}

// Updates event details where they are blank (eg: no requested_soc)
// @param	event[struct ChargeEvent*]		Pointer to a ChargeEvent
// @return	int								0|1 depending upon success. Can then be used to validate
int evse_event::complete_event_details(struct ChargeEvent *event) {
	
	// Update the details based on calculations where there is null data & create warnings if need be
	if(event->departure_time < 0) {
		gl_warning("Cannot determine the departure time");
		return 0;
	}
	if(event->arrival_time < 0) {
		gl_warning("Cannot determine the home arrival time");
		return 0;
	}
	if(event->arrival_soc < 0) {
		gl_warning("Cannot determine the arrival state of charge");
		return 0;
	}
	if(event->battery_size < 0) {
		gl_warning("Cannot determine the battery size");
		return 0;
	}
	if(event->requested_soc == -1) {
		// default to fully charged
		event->requested_soc = 100.0;
	}
	return 1;
}

// The ChargeEvent linked list is kept with the end element referencing the start element and these
// 		are held in cache.
// @param	t[TIMESTAMP]			The timestamp "t" to use to look up which event is the current valid one.
// @return	ChargeEvent*			The current event, based on the idea of no overlap (ie: uses first event with valid month & day of week that isn't currently at home). If NULL then at home
struct ChargeEvent *evse_event::get_charge_event(TIMESTAMP t){
	ChargeEvent	*tmp_charge_event, *return_charge_event;
	DATETIME	t_date;
	double		t_seconds, t_seconds_comparison, departure_time_offset;
	int 		overnight;

	// in a DATETIME format for each of comparisons
	gl_localtime(t,&t_date);
	t_seconds = datetime_time_to_double_seconds(&t_date);

	// gl_warning("Checking against time of day in seconds of: %f", t_seconds);

	// Go through the ChargeEvent objects looking for the one that first matches the current datetime
	//   it will only match if the day and month are valid and the vehicle is not currently at home
	//   if it returns NULL then the car is at home and thus charging
	tmp_charge_event	= charge_event_start;
	return_charge_event	= NULL;
	do {

		// Charge event must be valid for this month and day of week
		if( charge_event_valid_day_of_week(t_date.weekday,tmp_charge_event) && charge_event_valid_month_of_year(t_date.month,tmp_charge_event) ) {
			
			// not between arrival_time and departure_time accounting for overnight stays
			// Rotate times so that arrival_time is always at 0, thus we are then checking if t_seconds_comparison >= departure_time_seconds
			departure_time_offset	= 86400.0 * fmod(tmp_charge_event->departure_time - tmp_charge_event->arrival_time, 1.0);
			t_seconds_comparison	= fmod(t_seconds - 86400.0 * tmp_charge_event->arrival_time, 86400.0);
			
			// Rotation based on making the arrival time 0 -> fmod(time + (86400.0-arrival_time_seconds),86400.0)
			// Return if it's just arrived as it has priority
			if( t_seconds_comparison == 0 ) {
				return tmp_charge_event;
			}
			// Otherwise if you're on the event that's awesome too, but we'll check the rest first.
			else if (
				( t_seconds_comparison > 0 ) &&
				( t_seconds_comparison < departure_time_offset )
				) {
				gl_warning("now: %f dep: %f", t_seconds_comparison, departure_time_offset);	
				return_charge_event = tmp_charge_event;
			}
		}
		tmp_charge_event = tmp_charge_event->next;
	} while(tmp_charge_event != charge_event_start);
	
	// Return NULL if you are at home, or the next_charge_event if not.
	return return_charge_event;
}

// The ChargeEvent linked list is kept with the end element referencing the start element and these
// 		are held in cache.
// @param	t[TIMESTAMP]			The timestamp "t" to use to look up which event is the current valid one.
// @return	ChargeEvent*			The current event, based on the idea of no overlap (ie: uses first event with valid month & day of week that isn't currently at home) or the next valid event if it isn't on one at the moment.
struct ChargeEvent *evse_event::get_next_charge_event(TIMESTAMP t){
	ChargeEvent	*next_charge_event, *tmp_charge_event;
	TIMESTAMP	t_next_charge_event, t_tmp_charge_event;					// First valid timestamps of the charge events
	DATETIME	t_date,t_next_charge_event_date,t_tmp_charge_event_date;	
	double		t_seconds;
	double		tmp_charge_event_d, transition_offset_d, arrival_time_d, departure_time_d, destination_arrival_time_d, destination_departure_time_d;
	int 		i;

	gl_localtime(t,&t_date);
	t_next_charge_event = t + (TIMESTAMP)(86400.0*366); // increment by a full year to begin with
	t_tmp_charge_event	= t;							// Start at t for this - will be set on the first cycle through
	next_charge_event	= NULL;
	
	// Cycle through each day and then cycle through each of the charge_events
	// Only increment the day if there is no option for that day
	for(i = 0; (i < 366 && next_charge_event == NULL ); i++) {

		// Update the test time for each day of the test
		t_tmp_charge_event = t + (TIMESTAMP)(i * 86400.0);
		if ( i > 0 ) {
			// Reset to the zero hour of the day if it is not the day of "t" being tested
			t_tmp_charge_event -= (TIMESTAMP)(datetime_time_to_double_seconds(&t_date));

			// Timezone difference offset
			gl_localtime(t_tmp_charge_event,&t_tmp_charge_event_date);
			if (t_date.is_dst && !t_tmp_charge_event_date.is_dst) {
				t_tmp_charge_event += 3600;
			}
			else if (!t_date.is_dst && t_tmp_charge_event_date.is_dst) {
				t_tmp_charge_event -= 3600;
			}
		}
		// Extract to a local DATETIME object for verification of month and day
		gl_localtime(t_tmp_charge_event,&t_tmp_charge_event_date);

		// Start at the start
		tmp_charge_event = charge_event_start;

		// For each charge_event we want to see which is the closest
		// If no valid events for a given day, next_charge_event will still be NULL and so we will increment the day
		do {

			// Only check if the month and day is valid for the event
			if(charge_event_valid_day_of_week(t_tmp_charge_event_date.weekday,tmp_charge_event) && charge_event_valid_month_of_year(t_tmp_charge_event_date.month,tmp_charge_event)) {

				// DateTime of the test time (t or midnight of a following day)
				tmp_charge_event_d		 	= datetime_time_to_double_seconds(&t_tmp_charge_event_date); 
				// Do the same for each of the times for a event. A valid event only occurs if the time tmp_charge_event_d is not at home
				arrival_time_d				= 86400.0 * tmp_charge_event->arrival_time;
				departure_time_d			= 86400.0 * tmp_charge_event->departure_time;
				transition_offset_d			= 86400.0; // default to twenty four hours
			
				// Always has to be after the next_transition_start_d but also closer to the tmp_charge_event_d time
				if(arrival_time_d > tmp_charge_event_d && (arrival_time_d - tmp_charge_event_d) < transition_offset_d ) {
					transition_offset_d = arrival_time_d - tmp_charge_event_d;
				}
				if(departure_time_d > tmp_charge_event_d && (departure_time_d - tmp_charge_event_d) < transition_offset_d ) {
					transition_offset_d = departure_time_d - tmp_charge_event_d;
				}
				// 
				
				// The t_next_charge_event must be within the day ( tmp_charge_event_d + transition_offset_d < 86400.0 )
				// 	and be closer than the other t_next_charge_event
				if( ( tmp_charge_event_d + transition_offset_d < 86400.0 ) && ( (t_tmp_charge_event + (TIMESTAMP)(transition_offset_d) ) < t_next_charge_event ) ) {
					next_charge_event	= tmp_charge_event;
					t_next_charge_event	= (t_tmp_charge_event + (TIMESTAMP)(transition_offset_d));
				}
				
			}
			
			// Go to the next event
			tmp_charge_event = tmp_charge_event->next;
		} while(tmp_charge_event != charge_event_start);
	}
	
	return next_charge_event;
}

// This is tricky with multiple charge events for different days-of-week and months-of-year.
// @return t[TIMESTAMP]		The timestamp you want to discover the next transition for.
// @return TIMESTAMP		The timestamp of the next transition in charge events.
TIMESTAMP evse_event::get_next_transition(TIMESTAMP t) {
	ChargeEvent *event;
	TIMESTAMP	next_transition;
	DATETIME	next_transition_date;
	int			i;
	TIMESTAMP	next_transition_start_d, next_transition_offset_d, arrival_time_d, departure_time_d;
	
	// This is definitely the next one, so lets look for the best option.
	event = get_next_charge_event(t);
	
	// Cycle through a year
	for(i = 0; i < 366; i++) {

		next_transition = t + (TIMESTAMP)(i*86400);						// Increment the next_transition TIMESTAMP by a day
		gl_localtime(next_transition,&next_transition_date);			// Update the next_transition_date struct

		// If this day is valid then we are good to go and we have a base to work from for our "next transition"
		if(charge_event_valid_day_of_week(next_transition_date.weekday,event) && charge_event_valid_month_of_year(next_transition_date.month,event)) {

			// Set the transition to the start of the day unless it is the current timestamp
			if(next_transition > t) {
				next_transition = next_transition - (int)(datetime_time_to_double_seconds(&next_transition_date));
			}
			gl_localtime(next_transition,&next_transition_date);

			next_transition_start_d 		= datetime_time_to_double_seconds(&next_transition_date); // either 0 if not day of t, or the time of day if day of t. Thus transition is next_transition + ()
			arrival_time_d					= 86400.0 * event->arrival_time;
			departure_time_d				= 86400.0 * event->departure_time;
			next_transition_offset_d		= 86400.0; // default to twenty four hours
			
			// Always has to be after the next_transition_start_d but also 
			if(arrival_time_d > next_transition_start_d && (arrival_time_d - next_transition_start_d) < next_transition_offset_d ) {
				next_transition_offset_d = arrival_time_d - next_transition_start_d;
			}
			if(departure_time_d > next_transition_start_d && (departure_time_d - next_transition_start_d) < next_transition_offset_d ) {
				next_transition_offset_d = departure_time_d - next_transition_start_d;
			}
	
			// Only break if the next transition is after the current one
			if( next_transition_offset_d < 86400.0 && next_transition_offset_d > 0 ) {
				next_transition += (TIMESTAMP)(next_transition_offset_d);
				break;
			}
		}
	}
    gl_warning("now: %d nt: %d nto: %d", t, next_transition, next_transition_offset_d);
	
	return next_transition;
}

/* ------------------------------------------------------------------ 
   TIME to Hours, Seconds or Minutes for a given day helper functions
   ------------------------------------------------------------------ */

// Determines if a day of week is valid for a vehicle event
// @param	day_of_week[int]	An integer of the day of week as per Gridlab DATETIME
// @return	int					0 | 1 depending on truth
int evse_event::charge_event_valid_day_of_week(int day_of_week,struct ChargeEvent *event){
	// gl_verbose("Checking event %d day: %d&%d",event->event_id,event->days,int_to_bit_version(day_of_week+1));
	if(event->days == 0 || event->days & int_to_bit_version(day_of_week+1)) {
		return true;
	}
	return false;
}

// Determines if a month of year is valid for a vehicle event
// @param	month_of_year[int]	An integer of the month_of_year as per Gridlab DATETIME
// @return	int					0 | 1 depending on truth
int evse_event::charge_event_valid_month_of_year(int month_of_year, struct ChargeEvent *event){
	// gl_verbose("Checking event %d month: %d&%d",event->event_id,event->months,int_to_bit_version(month_of_year));
	if(event->months == 0 || event->months & int_to_bit_version(month_of_year)) {
		return true;
	}
	return false;
}

/* ------------
   Debug assist
   ------------ */

// Debug assist
void evse_event::gl_warning_charge_event(struct ChargeEvent *event) {
	if(event != NULL) {
		gl_warning("event_id: %d",event->event_id);
		gl_warning("months: %d",event->months);
		gl_warning("days: %d",event->days);
		gl_warning("arrival_time: %f",event->arrival_time);
		gl_warning("arrival_soc: %d",event->arrival_soc);		
		gl_warning("departure_time: %f",event->departure_time);
		gl_warning("requested_soc: %d",event->requested_soc);		
		gl_warning("battery_size: %d",event->battery_size);		
		gl_warning("*next: %d",event->next);
		gl_warning("self: %d",event);
	}
}


//////////////////////////////////////////////////////////////////////////
// IMPLEMENTATION OF CORE LINKAGE
//////////////////////////////////////////////////////////////////////////

EXPORT int create_evse_event(OBJECT **obj, OBJECT *parent)
{
	try
	{
		*obj = gl_create_object(evse_event::oclass);

		if (*obj!=NULL)
		{
			evse_event *my = OBJECTDATA(*obj,evse_event);
			gl_set_parent(*obj,parent);
			my->create();
			return 1;
		}
		else
			return 0;
	}
	CREATE_CATCHALL(evse_event);
}

EXPORT int init_evse_event(OBJECT *obj)
{
	try {
		if (obj!=NULL) {
			evse_event *my = OBJECTDATA(obj,evse_event);
			return my->init(obj->parent);
		} else {
			return 0;
		}
	}
	INIT_CATCHALL(evse_event);
}

EXPORT int isa_evse_event(OBJECT *obj, char *classname)
{
	if(obj != 0 && classname != 0){
		return OBJECTDATA(obj,evse_event)->isa(classname);
	} else {
		return 0;
	}
}

EXPORT TIMESTAMP sync_evse_event(OBJECT *obj, TIMESTAMP t1, PASSCONFIG pass)
{
	TIMESTAMP t2 = TS_NEVER;
	evse_event *my = OBJECTDATA(obj,evse_event);
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
	SYNC_CATCHALL(evse_event);
}

/**@}**/
