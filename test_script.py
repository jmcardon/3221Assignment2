#!/usr/bin/python2.7
import time
import pexpect
import sys
import random

# Sample input
correct_input = "2 I love operating systems!"

# Number of loop iterations
loop_iters = 500
# Responses to be parsed
expect_arr = ["Display thread 1: Received", "Display thread 2: Received"]
# Spawn My_Alarm as a child process
child = pexpect.spawn("./My_Alarm")
#Set error tolerance
offset_tolerance = 0.015
errors = 0


for _ in range(loop_iters):
    # Send input to process
    child.sendline(correct_input)
    # Parse which thread received the request
    index = child.expect(["Display thread 1: Received", "Display thread 2: Received"], timeout=1)
    # Measure the system time immediately after. Is subject to some error, as it's not at the same time stdout was printed
    # Also, from the time of receiving the alarm to printing there is a small delay,
    # as there are also delays due to locking of stdout.
    time_set = time.time()
    # Calculate the time offset to figure which thread the alarm should have gone.
    offset = time_set - int(time_set)
    if offset >= 0.5:
        time_offset = int(time_set) +1
    else:
        time_offset = int(time_set)

    # Output results
    if (time_offset % 2 == 0) and index == 1:
        print "[+] Correct. So far so good. Thread %d time %.3f" % (index+1, time_set)
    elif (time_offset % 2 != 0) and index == 0:
        print "[+] Correct. So far so good. Thread %d time %.3f" % (index+1, time_set)
    else:
        if (offset-0.5 <= offset_tolerance):
            print "[~] Error within 15msec tolerance bound in read error"
        else:
            print "[!] Uh oh. Error in Thread: %d with time %.3f"%(index+1, time_set)
            errors+=1
    time.sleep(random.random())

print "[.] Errors found: %d Error percent: %.3f%%" %(errors, errors*100/loop_iters)



