
.PHONY: all

all: np_simple np_single_proc np_multi_proc

np_simple: npshell.o process.o np_simple.o
	g++ $^ -std=c++11 -o $@

np_single_proc: rwg.o process.o np_single_proc.o
	g++ $^ -std=c++11 -o $@

np_multi_proc: rwg.o process.o np_multi_proc.o
	g++ $^ -std=c++11 -o $@

npshell.o: npshell.cpp process.h command.h server.h
	g++ -c $< -std=c++11 -o $@

process.o: process.cpp process.h command.h server.h
	g++ -c $< -std=c++11 -o $@

np_simple.o: np_simple.cpp process.h command.h server.h
	g++ -c $< -std=c++11 -o $@

rwg.o: rwg.cpp process.h command.h server.h rwg.h
	g++ -c $< -std=c++11 -o $@

np_single_proc.o: np_single_proc.cpp process.h command.h server.h rwg.h
	g++ -c $< -std=c++11 -o $@

np_multi_proc.o: np_multi_proc.cpp process.h command.h server.h rwg.h
	g++ -c $< -std=c++11 -o $@
