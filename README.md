# esp8266-event-notifier-neopixel
This is a fork of the Google Calendar Busy Indicator by fwacer. It connects over WiFi to a calendar, then turns a neopixel strip (or in my case, the neopixel shield for the D1 mini). This is used to display whether a person or a meeting room is free or busy based on their calendar. 
Eventually the plan is to use the 7segment display to show the next free meeting time.

You'll have to configure which pin is being used for the neopixels, and you can adjust the refresh timing to your liking.  Otherwise the code is mostly unchanged.

The following is fwacer's readme content for posterity.

Video of the original ESP8266 event notifier prototype (https://www.youtube.com/watch?v=q-8Iey-jSQw)

The underlying code is based on SensorsIOT's implementation of a similar idea: https://github.com/SensorsIot/Reminder-with-Google-Calender

Blog post: https://brycedombrowski.com/2020/05/spring-2020-google-calendar-busy-indicator/

# Instructions to make this project:
- Download _esp8266-event-notifier.ino_ to your project folder and modify it as needed. The #defines at the top contain most of the things you will want to modify.
- From the library HTTPSRedirect, download _HTTPSRedirect.h_ and _HTTPSRedirect.cpp_ and place them in your local project folder. https://github.com/electronicsguy/ESP8266/tree/master/HTTPSRedirect
- Copy/paste the contents of ReadCalendarEvent.gs to a new project file on script.google.com. Make sure to rename _\_calendarName_ to the name of your calendar. Then Publish->Deploy as web app . Make sure to "Anyone, even anonymous" when selecting "who has access to the app".  Copy the secret from the resulting URL: ```https://script.google.com/macros/s/XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX/exec```. The XXs are your secret. Note: every time you make a change to the code, you must increment the project version by 1 or the new code will not be live for your next request. For more information on this whole bullet point step, see Andreas Spiess's excellent video: https://youtu.be/sm1-l5-z3ag?t=199
- Create a file called _credentials.h_. It should contain the following:
  ```C++
  #define CREDENTIALS
  const char* SSID_NAME = "dlink"; // Your wifi's name
  const char* SSID_PASSWORD = "password"; // Your wifi password
  const char *GSCRIPT_ID =  "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"; //replace with your secret
  ```
