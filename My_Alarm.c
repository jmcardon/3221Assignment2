/*
 * alarm_mutex.c
 *
 * This is an enhancement to the alarm_thread.c program, which
 * created an "alarm thread" for each alarm command. This new
 * version uses a single alarm thread, which reads the next
 * entry in a list. The main thread places new requests onto the
 * list, in order of absolute expiration time. The list is
 * protected by a mutex, and the alarm thread sleeps for at
 * least 1 second, each iteration, to ensure that the main
 * thread can lock the mutex to add new work to the list.
 */
#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <stdio.h>


#define DISPLAY_ONE 1
#define DISPLAY_TWO 2
#define PRINT_INTERVAL 2
#define DATEFORMAT_SIZE 50
/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    struct timespec     time;   /* seconds from EPOCH */
    char                message[64];
    char                time_retrieved[DATEFORMAT_SIZE];
} alarm_t;

//Structure to pass onto display thread
//Contains a thread number, the alarm list specific to the thread, and the latest request in the
typedef struct display_struct {
    int thread_num;
    alarm_t * alarm_list;
    alarm_t * latest_request;

} disp_t;


//MUTEX for alarm thread
pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;

//MUTEX for display threads
pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;

//Global alarm list. this list should be written into then deleted from.
alarm_t *alarm_list = NULL;

//Display flag
volatile int display_flag = 0;
//Alarm flag
volatile int alarm_flag = 0;
//Date format
const char* date_format_string = "%Y-%m-%d %H:%M:%S";

/* Appends to the list of alarms, sorted by smallest time.
 *
 * If the alarm item finishes sooner than the old alarm,
 * Append to it before
 */
void appendToList(alarm_t ** base_list, alarm_t * new_item){

    alarm_t * old = *base_list;


    while (*base_list != NULL) {
        //If before another item, append.
        if (new_item->time.tv_sec <= old->time.tv_sec) {
            new_item->link = old;

            *base_list = new_item;
            break;
        }
        base_list = &((*base_list)->link);
        old = *base_list;
    }

    //End of list reached
    //Append new item to end of list
    if (*base_list == NULL) {
        *base_list = new_item;
    }

}

/* Thread function for the display of the alarms
 *
 */
void * display_thread(void * args){

    //Variables to print the current and time interval
    time_t print_time;
    //flag for whether the print_time variable has been reset
    int print_flag = 0;

    //Reference to previous alarm to be free
    alarm_t * oldref;
    //the display struct.
    disp_t * display = (disp_t *) args;
    //Structure to acquire current time with nanosec precision
    struct timespec now;
    //Double precision floating point for time comparisons
    double time_nsec, alarm_time;
    //Time printing struct;
    struct tm local_time, * err_check;
    char local_time_str[DATEFORMAT_SIZE];
    char expiration_str[DATEFORMAT_SIZE];


    while (1){

        /* If the display flag is not set, loop on the alarm list
         * If it's empty, do nothing. If it's not, loop printing out
         * every two seconds. Free the alarm after the duration.
         * If there's another alarm in the queue, resume that alarm.
         *
         * It is done this way due to sleep() pausing the entire thread.
         */
        while(display->thread_num != display_flag){
            if(display->alarm_list != NULL){

                //Calculate the current time in seconds.
                clock_gettime(CLOCK_REALTIME, &now);
                time_nsec = ((double)now.tv_nsec) * 1e-9 + (double)now.tv_sec;

                //Print flag is set in case we break out of the loop, then we know to
                //re-set the time interval
                if(print_flag == 0){
                    flockfile(stdout);
                    printf("Display thread %d: Number of SecondsLeft %d: Time:%s alarm request: number of seconds: %d message: %s\n",
                           display->thread_num,
                           display->alarm_list->time.tv_sec - now.tv_sec,
                           display->alarm_list->time_retrieved,
                           display->alarm_list->seconds,
                           display->alarm_list->message);
                    fflush(stdout);
                    funlockfile(stdout);
                    //Set the print time, rounding to seconds is okay
                    print_time = now.tv_sec + PRINT_INTERVAL;
                    //Set the precise alarm time.
                    alarm_time = (double)display->alarm_list->time.tv_nsec*1e-9 + display->alarm_list->time.tv_sec;
                    print_flag = 1;
                    continue;
                }

                //If the current time is greater than or equal to the target time
                //Print and free.
                if(time_nsec >= alarm_time){
                    //Print alarm done and a newline for the user to display alarm
                    //Get the local time
                    err_check = localtime_r(&(display->alarm_list->time.tv_sec), &local_time);
                    if(err_check == NULL)
                        fprintf(stderr, "Error Acquiring local time\n");

                    strftime(local_time_str, DATEFORMAT_SIZE, date_format_string, &local_time);

                    flockfile(stdout);
                    printf("\nDisplay Thread  %d: Alarm expired at %s: %s\n",
                           display->thread_num,
                           local_time_str,
                           display->alarm_list->message);
                    printf("alarm>");
                    fflush(stdout);
                    funlockfile(stdout);
                    oldref = display->alarm_list;

                    //If there is a next item in the list, free the old reference and move to that one.
                    if((display->alarm_list)->link != NULL){
                        display->alarm_list = (display->alarm_list)->link;
                        free(oldref);
                    }
                        //Otherwise, just free the reference and null.
                    else {
                        free(oldref);
                        display->alarm_list = NULL;
                    }
                    //Set print flag to 0 to acquire new print interval
                    print_flag = 0;
                    continue;
                }

                // If the current time has finally reached the print time,
                // Print. Note: Should be error checked.
                // now should never actually excede print_time.
                if(now.tv_sec >= print_time){

                    print_time = now.tv_sec + PRINT_INTERVAL;

                    flockfile(stdout);
                    printf("\nDisplay thread %d: Number of SecondsLeft %d: Time:%s alarm request: number of seconds: %d message: %s",
                           display->thread_num,
                           display->alarm_list->time.tv_sec - now.tv_sec,
                           display->alarm_list->time_retrieved,
                           display->alarm_list->seconds,
                           display->alarm_list->message);
                    fflush(stdout);
                    funlockfile(stdout);

                }
            }

            //Do nothing if it's null

        }
        //Lock the display thread to make sure the append operation to the list is atomic.
        pthread_mutex_lock(&display_mutex);

        //Set time
        clock_gettime(CLOCK_REALTIME, &now);

        //Get the time the request was received
        err_check = localtime_r(&(now.tv_sec),&local_time);
        if(err_check == NULL)
            fprintf(stderr, "Error Acquiring local time\n");


        strftime(display->latest_request->time_retrieved,DATEFORMAT_SIZE,date_format_string,&local_time);

        //Get time alarm expires
        err_check = localtime_r(&(display->alarm_list->time.tv_sec), &local_time);
        if(err_check == NULL)
            fprintf(stderr, "Error Acquiring local time\n");

        strftime(expiration_str, DATEFORMAT_SIZE,date_format_string, &local_time);

        //Display the last request received.
        printf("Display thread %d: Received Alarm Request at time %s: number of seconds: %d message: %s, ExpiryTime is %s\n",
               display->thread_num,
               display->latest_request->time_retrieved,
               display->latest_request->seconds,
               display->latest_request->message,
               expiration_str);


        //Unlock the display mutex.
        pthread_mutex_unlock(&display_mutex);
        //Unlock the display flag back to the alarm thread
        display_flag = 0;
        //Unlock the main thread print
        alarm_flag = 0;


        print_flag = 0;

    }
}

/*
 * The alarm thread
 */
void *alarm_thread (void *arg)
{
    alarm_t *alarm;
    int status;
    //The structures to pass data to the display threads.
    disp_t * display_one, * display_two;
    //The threads
    pthread_t display_thread1, display_thread2;
    //Precision time checks.
    float nano_time;
    time_t sec_time;
    //String format time
    struct tm alarm_local_time, * err_check;
    char alarm_local_str[DATEFORMAT_SIZE];


    //Set the struct for the first thread
    display_one = malloc(sizeof(disp_t));
    if(display_one == NULL)
        err_abort(EXIT_FAILURE, "Display one allocation failed");

    display_one->thread_num = DISPLAY_ONE;
    display_one->alarm_list = NULL;

    //Set the struct for the second thread
    display_two = malloc(sizeof(disp_t));
    if(display_two == NULL)
        err_abort(EXIT_FAILURE, "Display two allocation failed");

    display_two->alarm_list = NULL;
    display_two->thread_num = DISPLAY_TWO;

    //Create thread one
    status = pthread_create (
            &display_thread1, NULL, display_thread, (void *)display_one);
    if (status != 0)
        err_abort (status, "Create display thread 1");

    //Create thread two
    status = pthread_create (
            &display_thread2, NULL, display_thread, (void *)display_two);
    if (status != 0)
        err_abort (status, "Create display thread 2");

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits.
     */
    while (1) {
        //Check for the flag to see whether a display thread is currently performing an operation.
        while(display_flag != 0);
        //Lock the display mutex.
        pthread_mutex_lock(&display_mutex);
        //Block thread until main thread has actually receive a request.
        while(alarm_flag == 0);
        alarm = alarm_list;
        //Receive mutex to assure mutual exclusion.
        status = pthread_mutex_lock (&alarm_mutex); //Block
        if (status != 0)
            err_abort (status, "Lock mutex");

        nano_time = (float)alarm->time.tv_nsec*1e-9;
        sec_time = alarm->time.tv_sec;

        if(nano_time >= 0.5)
            sec_time += 1;


        //Get the current time
        err_check = localtime_r(&(alarm->time.tv_sec),&alarm_local_time);
        if(err_check == NULL)
            fprintf(stderr, "Error Acquiring local time\n");

        strftime(alarm_local_str,DATEFORMAT_SIZE,date_format_string,&alarm_local_time);



        //If the time is even, send to display two, otherwise
        //Send to display one
        if((sec_time % 2) == 0) {
            display_flag = DISPLAY_TWO;
            appendToList(&(display_two->alarm_list), alarm);
            display_two->latest_request = alarm;
            printf("Alarm Thread passed Alarm Request to Display Thread %d at %s: number of seconds: %d message: %s\n",
                   DISPLAY_TWO,
                   alarm_local_str,
                   alarm->seconds,
                   alarm->message);
        }
        else {
            display_flag = DISPLAY_ONE;
            appendToList(&(display_one->alarm_list), alarm);
            display_one->latest_request = alarm;
            printf("Alarm Thread passed Alarm Request to Display Thread %d at %s: number of seconds: %d message: %s\n",
                   DISPLAY_ONE,
                   alarm_local_str,
                   alarm->seconds,
                   alarm->message);
        }
        //Get rid of the reference.
        alarm_list = NULL;

        //Unlock the display thread that received the request.
        status = pthread_mutex_unlock(&display_mutex);
        if (status != 0)
            err_abort (status, "Unlock display mutex");


        //Unlock the main thread after the transaction.
        status = pthread_mutex_unlock (&alarm_mutex);
        if (status != 0)
            err_abort (status, "Unlock mutex");

    }
}

int main (int argc, char *argv[])
{
    int status;
    char line[128];
    alarm_t *alarm;
    pthread_t thread;
    struct tm main_local_time, * err_check;
    char main_local_str[DATEFORMAT_SIZE];


    //Create the alarm thread;
    status = pthread_create (
            &thread, NULL, alarm_thread, NULL);


    if (status != 0)
        err_abort (status, "Create alarm thread");


    /* Main Event loop
     * Wait for stdin, parse if correct, then allocate to an alarm
     * otherwise, try again.[
     *
     */
    while (1) {


        while(alarm_flag != 0);


        printf ("alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        if (alarm == NULL)
            errno_abort ("Allocate alarm");

        /*
         * Parse input line into seconds (%d) and a message
         * (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         */
        if (sscanf (line, "%d %64[^\n]",
                    &alarm->seconds, alarm->message) < 2) {
            fprintf (stderr, "Bad command\n");
            free (alarm);
            continue;
        } else {
            //Lock the thread
            status = pthread_mutex_lock (&alarm_mutex);
            if (status != 0)
                err_abort (status, "Lock mutex");


            //Allocate the time
            clock_gettime(CLOCK_REALTIME, &(alarm->time));
            //Set alarm time
            alarm->time.tv_sec += alarm->seconds;
            //get the local time string
            err_check = localtime_r(&(alarm->time.tv_sec),&main_local_time);
            if(err_check == NULL)
                fprintf(stderr, "Error Acquiring local time\n");

            strftime(main_local_str,DATEFORMAT_SIZE,date_format_string,&main_local_time);

            flockfile(stdout);
            //Output message to console
            printf("Main Thread Received Alarm Request at %s: %d seconds with message: %s\n",
                   main_local_str, alarm->seconds, alarm->message);
            fflush(stdout);
            funlockfile(stdout);

            //Set alarm list to the current alarm. NULL the next in sequence
            alarm_list = alarm;
            alarm_list->link = NULL;
            //Set flag for alarm thread to wait on the mutex
            alarm_flag = 1;

            //Unlock the alarm thread
            status = pthread_mutex_unlock (&alarm_mutex);


            if (status != 0)
                err_abort (status, "Unlock mutex");
        }
    }
}
