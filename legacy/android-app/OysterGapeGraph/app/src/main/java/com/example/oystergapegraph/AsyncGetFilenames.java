package com.example.oystergapegraph;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.AsyncTask;
import android.view.View;
import android.widget.Button;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.util.ArrayList;
import java.util.List;

public class AsyncGetFilenames extends AsyncTask<Void,Integer, List<String>> {
    ProgressBar progressBar;
    TextView textView;
    Context context;
    Button retry_button;

    int progressCount;
    static final int number_of_tasks = 2;
    AsyncGetFilenames(Context context , ProgressBar progress, TextView text, Button retry_button)
    {
        this.progressBar = progress;
        this.textView = text;
        this.context = context;
        this.retry_button = retry_button;
        progressCount = 0;
    }
    @Override
    protected List<String> doInBackground(Void... voids) {
        MbraceFTP mbraceFTP = new MbraceFTP();
        if(!MbraceFTP.ftp_client.isConnected())
        {
            UpdateProgress.update_progress_text(textView,"Connecting To Server...");
            mbraceFTP.connect_to_server();
            if(!MbraceFTP.ftp_client.isConnected())
            {
                return null;
            }
        }
        publishProgress(1);
        UpdateProgress.update_progress_text(textView,"Getting Filenames...");
        List<String> files = mbraceFTP.Get_Bin_Data_Files();
        publishProgress(1);
        return files;
    }

    @Override
    protected void onProgressUpdate(Integer... values) {
        super.onProgressUpdate(values);
        progressCount += values[0];
        UpdateProgress.update_progress_bar(progressBar,progressCount,number_of_tasks);
    }

    @Override
    protected void onPostExecute(List<String> strings) {
        super.onPostExecute(strings);
        if(strings != null)
        {
            Intent move_to_file_select = new Intent(context, FileSelectActivity.class);
            move_to_file_select.putStringArrayListExtra("FILENAMES", (ArrayList<String>)strings);
            textView.post(new Runnable() {
                @Override
                public void run() {
                    textView.setText("Done!");
                }
            });
            context.startActivity(move_to_file_select);
            ((Activity)context).finish();
            retry_button.setVisibility(View.INVISIBLE);
        }
        else
        {
            UpdateProgress.update_progress_text(textView, "Unable to connect to server. Check connection.");
            retry_button.setVisibility(View.VISIBLE);
        }
    }
}
