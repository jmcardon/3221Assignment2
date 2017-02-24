#commands: make, make clean
HEADERS = errors.h
OBJECTS = My_Alarm.o

default: My_Alarm

#$< refers to the original object, $@ the target object
%.o: %.c $(HEADERS)
	cc -c $< -o $@ -lrt -lpthread

My_Alarm: $(OBJECTS)
	cc $(OBJECTS) -o $@ -lrt -lpthread

clean: 
	-rm -f $(OBJECTS)
	-rm -f My_Alarm
