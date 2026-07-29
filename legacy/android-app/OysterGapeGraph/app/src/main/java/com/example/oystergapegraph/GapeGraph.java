package com.example.oystergapegraph;

import android.graphics.Color;

import com.github.mikephil.charting.charts.LineChart;
import com.github.mikephil.charting.data.Entry;
import com.github.mikephil.charting.data.LineData;
import com.github.mikephil.charting.data.LineDataSet;
import com.github.mikephil.charting.interfaces.datasets.ILineDataSet;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;

public class GapeGraph {
    private LineChart chart;
    int number_of_sensors = 6;
    GapeGraph(LineChart view_chart){
        chart = view_chart;
    }
    static private List<ILineDataSet> sensor_datasets;

    public void Set_Up_Graph(char[] data_in_bytes, String fileName, boolean[] checkBox) {
        List<List<Entry>> sensors = new ArrayList<>();
        List<Character> gape_data = get_gape_data(data_in_bytes);
        for(int i = 0; i < number_of_sensors; i++)
        {
            if(checkBox[i]) sensors.add(get_sensor_data(i, gape_data));
            else sensors.add(null);
        }
        sensor_datasets = Create_Sensor_DataSets(sensors, checkBox);
        LineData data = new LineData(sensor_datasets);
        chart.setData(data);
        String fileName_NoExtension = fileName.split(Pattern.quote("."))[0];
        chart.getDescription().setText(fileName_NoExtension);
        chart.invalidate();
    }

    public void Reset_Chart()
    {
        chart.invalidate();
    }

    static public List<ILineDataSet> Get_Sensor_Data_Sets()
    {
        return sensor_datasets;
    }

    private List<ILineDataSet> Create_Sensor_DataSets(List<List<Entry>> sensors, boolean[] checkBox)
    {
        List<ILineDataSet> dataSets = new ArrayList<>();
        int color_orange = 0xffff8c00;
        int[] sensor_color = new int[]{Color.BLUE, color_orange, Color.GREEN, Color.RED, Color.CYAN, Color.MAGENTA};
        for(int i = 0; i < sensors.size(); i++)
        {
            LineDataSet set = new LineDataSet(sensors.get(i), Integer.toString(i + 1));
            set.setColor(sensor_color[i]);
            set.setDrawValues(false);
            set.setDrawCircles(false);
            dataSets.add(set);
            if(!checkBox[i])
            {
                set.setVisible(false);
            }
        }
        return dataSets;
    }

    private List<Character> get_gape_data(char[] bytes)
    {
        List<Character> gape_data = new ArrayList<>();
        int pos = 8;
        while(pos < bytes.length)
        {
            if(pos % 68 == 0) pos += 8;
            gape_data.add(bytes[pos++]);
        }
        return gape_data;
    }

    private List<Entry> get_sensor_data(int sensor_number, List<Character> bytes)
    {
        List<Entry> sensor_values = new ArrayList<>();
        int pos = sensor_number;

        int compression_size = 1000;

        List<Float> compressed_data = new ArrayList<>();
        while(pos < bytes.size())
        {
            compressed_data.add((float)bytes.get(pos));
            pos += number_of_sensors;
        }
        compressed_data = Compress_Data(compressed_data, compression_size);

        for(int i = 0; i < compressed_data.size(); i++)
        {
            sensor_values.add(new Entry(i, compressed_data.get(i)));
        }
        return sensor_values;
    }

    private List<Float> Compress_Data(List<Float> data, int size)
    {
        List<Float> compressed_data = new ArrayList<>();
        float[] bytes_to_average = new float[size];
        int c = 0;
        for(int i = 0; i < data.size(); i++)
        {
            c = i % size;
            if(c == 0)
            {
                compressed_data.add(Average(bytes_to_average));
            }
            bytes_to_average[c] = data.get(i);
            if(i + 1 >= data.size()){
                compressed_data.add(Average(bytes_to_average));
            }
        }
        return compressed_data;
    }

    private float Average(float[] bytes)
    {
        int sum = 0;
        for(int i = 0; i < bytes.length; i++)
        {
            sum += bytes[i];
        }
        float avg = sum/bytes.length;
        return avg;
    }
}
