%Written by James Curtis Addy

%State the file name, bytes per timestamp, number of sensors, and how many times to average the sensor values.
file_name =  'BC_DD_C2_81_05_2A_2018-07-01.bin';
bytes_per_timestamp = 68;
number_of_sensors = 6;
times_averaged = 10;

%Open the file and read the data from it.
file_id = fopen(file_name);
data = fread(file_id);
fclose(file_id);

%Rearrange the data so that the milliseconds timestamps and sensor data
%are organized in columns.
data = reshape(data, bytes_per_timestamp, []);
data = transpose(data);

%Grab the bytes representing the milliseconds timestamps from the data matrix (columns 3,4,5,6)
millis_bytes = data(:, 3:6);

%Grab the sensor values from the data matrix (columns 9 to end)
sensor_values = data(:,9:end);

%Arrange the sensor values into a single column
sensor_values = transpose(sensor_values);
sensor_values = sensor_values(:);

%Stack the values so that each column contains the values for the respective sensors.
%Column1 values will be sensor1's readings, column2 will be sensor2's, and so forth.
row_count = size(sensor_values, 1) / number_of_sensors;
sensor_values = reshape(sensor_values, row_count, number_of_sensors);

%Convert each 4 byte sequence of millis_bytes into a single 32bit value.
%This 32 bit value is the timestamp in milliseconds.
millis_values = convert_ms_bytes_to_integer(uint32(millis_bytes));

%Average data based on the times_averaged.
number_of_rows = size(sensor_values,1) / times_averaged;
averaged_matrix = zeros(number_of_rows, number_of_sensors);
for i = 1:number_of_sensors
    sensor_column = sensor_values(:,i);
    columns_to_avg = reshape(sensor_column,times_averaged,[]);
    averaged_readings = transpose(mean(columns_to_avg));
    averaged_matrix(:,i) = averaged_readings;
end
sensor_values = averaged_matrix;

%Normalize the data using the minimum of each column subtracted from its respective column.
normalized_values = sensor_values - min(sensor_values);

%Plot the values.
plot(normalized_values);

%This function shifts the milliseconds byte values into the proper position inside the 32 bit integers.
%Functions need to be at the bottom of the script.
function values = convert_ms_bytes_to_integer(millis_bytes)
    values = zeros(size(millis_bytes, 1), 1);
    for i = 1:size(values,1)
        %This method uses bitshifting to set the bits of the 32 bit value
        shift_24 = bitshift(millis_bytes(i,1),24);
        shift_16 = bitshift(millis_bytes(i,2),16);
        shift_8 = bitshift(millis_bytes(i,3),8);
        shift_0 = millis_bytes(i,4);
        sum = shift_24 + shift_16 + shift_8 + shift_0;
        values(i,1) = sum;
    end
end





