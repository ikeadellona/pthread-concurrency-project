Ikea Dellona
V01043794
CSC 360 Assignment 2

Program ACS (Airline Check-In System) simulates customer arrivals using POSIX thread and places them into either an economy queue or business queue. The system creates 5 clerk threads that serve customers and follow priority service for the business queue. The program also show statistics for the average waiting time of all customer, business-class customers, and economy-class customers.

How to use ACS
Step 1: compile test files that you want to use. Ensure that the input file is in the proper format (first line: number of customers, the following lines: customer_id:class_type,arrival_time,service_time).
Step 2: execute "make" in terminal to compile ACS
Step 3: execute "./ACS inputfile.txt" 
