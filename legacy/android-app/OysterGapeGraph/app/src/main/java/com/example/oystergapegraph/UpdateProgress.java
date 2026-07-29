package com.example.oystergapegraph;

import android.util.Log;
import android.view.View;
import android.widget.ProgressBar;
import android.widget.TextView;

public class UpdateProgress {
    static void update_progress_bar(ProgressBar progressBar, int progress_count, int number_of_tasks)
    {
        float ratio = (float)progress_count/(float)number_of_tasks;
        int  percentage =  (int)(ratio * 100);
        progressBar.setProgress(percentage);
        Log.d("Percentage" , Integer.toString(percentage));
    }

    static void update_progress_text(final TextView textView, final String text)
    {
        textView.post(new Runnable()
        {
            @Override
            public void run()
            {
                textView.setText(text);
            }
         });
    }

    static void turn_text_view_invisible(final TextView textView)
    {
        textView.post(new Runnable(){
            @Override
            public void run() {
                textView.setVisibility(View.INVISIBLE);
            }
        });
    }
}
