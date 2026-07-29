import Mbrace

filename = "24_6F_28_A0_DA_30_2019-12-06.bin"

mb_data = Mbrace.Mbrace_Data(filename)

gape_readings = mb_data.Gape_Readings
temperature_readings = mb_data.Temperature_Readings
millis_timestamps = mb_data.Millis_Timestamps
server_timestamps = mb_data.Server_Timestamps

print("First 4 gape readings")# ( minus the minimum row )")
print(gape_readings[0:4, :])# - gape_readings.min(0))

print("\nFirst 10 timestamps ( in seconds )")
print(millis_timestamps[0:10, :] / 1000)

print("\nFirst 10 server timestamps ")
print(server_timestamps[0:10, :])

#Multiply by floats to ensure the data type is float not byte
hour_to_seconds = server_timestamps[0:10, 0] * 3600.0
min_to_seconds = server_timestamps[0:10, 1] * 60.0
seconds = hour_to_seconds + min_to_seconds + server_timestamps[0:10, 2]
print("\nServer time to seconds (first 10) ")
print(seconds)

if temperature_readings is not None:
    print("\nFirst 4 temperature readings ( in Celcius )")
    print(temperature_readings[0:4])
    print("\nFirst 4 temperature readings ( in Fahrenheit )")
    print((temperature_readings[0:4] * 1.8) + 32)
