package com.example.oystergapegraph;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.View;
import android.widget.CheckBox;

import com.github.mikephil.charting.charts.LineChart;
import com.github.mikephil.charting.data.LineDataSet;
import com.github.mikephil.charting.interfaces.datasets.ILineDataSet;

public class GraphActivity extends AppCompatActivity {
    GapeGraph gapeGraph;
    boolean[] checkbox = {true, true, true, true, true, true};

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_graph);
        gapeGraph = new GapeGraph((LineChart) findViewById(R.id.line_chart));
        gapeGraph.Set_Up_Graph(RetrieveDataActivity.data, RetrieveDataActivity.file_name, checkbox);
    }

    public void On_CheckBox_Clicked(View view) {
        boolean checked = ((CheckBox) view).isChecked();

        switch (view.getId()) {
            case R.id.checkbox1:
                checkbox[0] = checked;
                Make_Dataset_Invisible(0, checked);
                break;
            case R.id.checkbox2:
                checkbox[1] = checked;
                Make_Dataset_Invisible(1, checked);
                break;
            case R.id.checkbox3:
                checkbox[2] = checked;
                Make_Dataset_Invisible(2, checked);
                break;
            case R.id.checkbox4:
                checkbox[3] = checked;
                Make_Dataset_Invisible(3, checked);
                break;
            case R.id.checkbox5:
                checkbox[4] = checked;
                Make_Dataset_Invisible(4, checked);
                break;
            case R.id.checkbox6:
                checkbox[5] = checked;
                Make_Dataset_Invisible(5, checked);
                break;
        }
        gapeGraph.Reset_Chart();
    }

    private void Make_Dataset_Invisible(int index, boolean checked)
    {
        ILineDataSet set = GapeGraph.Get_Sensor_Data_Sets().get(index);

        set.setVisible(checked);
    }
}
