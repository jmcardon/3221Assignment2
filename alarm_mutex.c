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


#define DISPLAY_ONE 1
#define DISPLAY_TWO 2
#define PRINT_INTERVAL 2
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
    time_t              time;   /* seconds from EPOCH */
    char                message[64];
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

/* Appends to the list of alarms, sorted by smallest time.
 *
 * If the alarm item finishes sooner than the old alarm,
 * Append to it before
 */
void appendToList(alarm_t ** base_list, alarm_t * new_item){

    alarm_t * old = *base_list;


    while (*base_list != NULL) {
        //If before another item, append.
        if (new_item->time <= old->time) {
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
    time_t now, print_time;
    //flag for whether the print_time variable has been reset
    int print_flag = 0;

    //Reference to previous alarm to be free
    alarm_t * oldref;
    //the display struct.
    disp_t * display = (disp_t *) args;



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
                now = time(NULL);

                //Print flag is set in case we break out of the loop, then we know to
                //re-set the time interval
                if(print_flag == 0){
                    print_time = now + PRINT_INTERVAL;
                    print_flag = 1;
                    continue;
                }

                //If the current time is greater than or equal to the target time
                //Print and free.
                //TODO: Re-calibrate to nanoseconds for more accuracy. We have to use nano for selecting the display anyway
                if(now >= display->alarm_list->time){
                    //Print alarm done and a newline for the user to display alarm
                    //NOTE: Currently broken. For some reason alarm> prints in the line above alarm done
                    printf("\nAlarm done: %s\nalarm>", display->alarm_list->message);
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
                }

                // If the current time has finally reached the print time,
                // Print. Note: Should be error checked.
                // now should never actually excede print_time.
                if(now >= print_time){

                    print_time = now + PRINT_INTERVAL;

                    printf("\nDisplay thread %d: Number of SecondsLeft %d: Time: alarm request:%s\nalarm>",
                           display->thread_num, display->alarm_list->time - now
                            , display->alarm_list->message);
                }
            }

            //Do nothing if it's null

        }
        //Lock the display thread to make sure the append operation to the list is atomic.
        pthread_mutex_lock(&display_mutex);

        now = time(NULL);

        //Display the last request received.
        printf("Display thread %d: Received Alarm Request at time %ld: %s, ExpiryTime is %ld\n",
               display->thread_num,
               now, display->latest_request->message,
               display->latest_request->time);


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


        //If the time is even, send to display two, otherwise
        //Send to display one
        if((alarm->time % 2) == 0) {
            display_flag = DISPLAY_TWO;
            appendToList(&(display_two->alarm_list), alarm);
            display_two->latest_request = alarm;
            printf("Passed Alarm to display 2\n");
        }
        else {
            display_flag = DISPLAY_ONE;
            appendToList(&(display_one->alarm_list), alarm);
            display_one->latest_request = alarm;
            printf("Passed Alarm to display 1\n");
        }
        //Get rid of the reference.
        alarm_list = NULL;

        //Unlock the display thread that received the request.
        status = pthread_mutex_unlock(&display_mutex);
        if (status != 0)
            err_abort (status, "Unlock display mutex");


#ifdef DEBUG
        printf ("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                sleep_time, alarm->message);
#endif

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
    alarm_t *alarm, **last, *next;
    pthread_t thread;


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


            alarm->time = time (NULL) + alarm->seconds;
            //Output message to console
            printf("Main Thread Received Alarm Request at %lld: %d seconds with message: %s\n",
                   alarm->time - alarm-> seconds, alarm->seconds, alarm->message);

            //Set alarm list to the current alarm. NULL the next in sequence
            alarm_list = alarm;
            alarm_list->link = NULL;
            //Set flag for alarm thread to wait on the mutex
            alarm_flag = 1;


#ifdef DEBUG
            printf ("[list: ");
            for (next = alarm_list; next != NULL; next = next->link)
                printf ("%d(%d)[\"%s\"] ", next->time,
                    next->time - time (NULL), next->message);
            printf ("]\n");
#endif

            //Unlock the alarm thread
            status = pthread_mutex_unlock (&alarm_mutex);


            if (status != 0)
                err_abort (status, "Unlock mutex");
        }
    }
}
