# weather-broadcast-simulator
Distributed weather broadcast simulator using C++ server and Python GUI client with bidirectional TCP/UDP communication.

## Overview

This project simulates a weather broadcasting service where a server generates
weather data and sends it to multiple clients.

The system uses:

- TCP for configuration and client registration
- UDP for real-time weather updates
- Multithreading for handling multiple clients concurrently

The client application includes a graphical interface for displaying weather
information such as temperature, humidity, wind speed and weather conditions.

## Technologies Used

- C++ (Server)
- Python + PyQt6 (Client GUI)
- TCP Sockets
- UDP Sockets
- Multithreading
- Cross-platform networking

## Architecture

The system consists of two main components:

### Server (C++)

The server is responsible for:

- accepting client connections
- handling configuration requests via TCP
- generating simulated weather data
- broadcasting weather updates via UDP
- managing multiple clients concurrently using threads

#### Communication protocols

TCP is used for:

- client registration
- setting the selected city
- configuration messages

UDP is used for:

- real-time weather data broadcasting
- fast delivery of updates to all clients


### Client (Python GUI)

The client application provides a graphical interface that allows the user to:

- connect to the server
- select a city
- receive weather updates
- visualize weather data

The GUI displays:

- temperature
- humidity
- wind speed
- weather condition icon

Weather updates are received via **UDP**, while configuration messages are sent via **TCP**.


## Features

- Bidirectional communication (TCP + UDP)
- Concurrent client handling
- Graphical user interface
- Configurable cities
- Weather icons

## How to Run

### Server

Compile and run the C++ server:
g++ WeatherServer.cpp -o server
./server


### Client

Run the Python client:
python client.py


## Project Structure

server/
main.cpp
favs
client/
client.py
gui.py
assets/
icons/


## Author

Student networking project demonstrating socket programming,
multithreading and client–server architecture.
