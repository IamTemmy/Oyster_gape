package com.example.oystergapegraph;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;

public class AcquireFilenamesActivity extends AppCompatActivity {
    ProgressBar progressBar;
    TextView textView;
    Button retry_button;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_acquire_filenames);
        progressBar = findViewById(R.id.progressAcquireFiles);
        retry_button = findViewById(R.id.retry_button);
        retry_button.setVisibility(View.INVISIBLE);
        textView = findViewById(R.id.textAcquireFiles);
        Begin_Filename_Task();
        retry_button.setOnClickListener(new View.OnClickListener()
        {
            @Override
            public void onClick(View v) {
                Begin_Filename_Task();
            }
        });
    }

    private void Begin_Filename_Task()
    {
        AsyncGetFilenames get_filenames_task = new AsyncGetFilenames(this, progressBar, textView, retry_button);
        get_filenames_task.execute();
    }
}
