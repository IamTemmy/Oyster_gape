package com.example.oystergapegraph;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.widget.ProgressBar;
import android.widget.TextView;

public class RetrieveDataActivity extends AppCompatActivity {
    TextView textView;
    ProgressBar progressBar;
    static public String file_name;
    static public char[] data;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_retrieve_data);
        progressBar = findViewById(R.id.graphProgressBar);
        textView = findViewById(R.id.graphLoadingText);
        AsyncGraphGapeData gape_graph_task = new AsyncGraphGapeData(this, textView, progressBar);
        file_name = getIntent().getStringExtra("GRAPHFILE");
        gape_graph_task.execute(file_name);
    }

}
