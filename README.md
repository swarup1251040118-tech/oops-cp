Setup (Ubuntu / WSL):
  apt update && apt install -y g++ libsqlite3-dev unzip
  unzip bike-e20-simple.zip && cd bike-e20-simple
  bash run.sh
Open http://localhost:8080
(To reload the 50 bikes after editing data/bikes.csv: delete bikes.db)

OOP: Database (encapsulation) | Vehicle (abstraction) -> Bike -> Commuter/Sports/Cruiser/Scooter (inheritance, polymorphism via virtual methods) | MileageCalculator, MaintenanceReminder (objects)
