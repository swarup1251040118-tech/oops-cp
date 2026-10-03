#!/bin/bash
cd "$(dirname "$0")"
g++ -std=c++17 -O2 main.cpp -o bike_e20 -lsqlite3 && ./bike_e20
