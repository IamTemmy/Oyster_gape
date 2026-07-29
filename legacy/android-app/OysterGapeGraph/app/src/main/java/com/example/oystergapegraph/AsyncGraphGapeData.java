package com.example.oystergapegraph;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.os.AsyncTask;
import android.util.Log;
import android.view.View;
import android.widget.ProgressBar;
import android.widget.TextView;

public class AsyncGraphGapeData extends AsyncTask<String, Integer, char[]> {
    TextView textView;
    ProgressBar progressBar;
    Context context;
    String fileName;

    static final int number_of_tasks = 2;
    int progress_count;

    AsyncGraphGapeData(Context context, TextView textView, ProgressBar progressBar){
        this.context = context;
        this.textView = textView;
        this.progressBar = progressBar;
        progress_count = 0;
    }

    @Override
    protected void onPreExecute() {
        super.onPreExecute();
    }

    @Override
    protected char[] doInBackground(String... file) {
        fileName = file[0];
        MbraceFTP mbraceFTP = new MbraceFTP();
        char[] bytes;
        if(!mbraceFTP.ftp_client.isConnected())
        {
            UpdateProgress.update_progress_text(textView,"Connecting To Server...");
            mbraceFTP.connect_to_server();
            if(!mbraceFTP.ftp_client.isConnected())
            {
                return null;
            }
        }
        publishProgress(1);
        UpdateProgress.update_progress_text(textView,"Retrieving Data From Server...");
        bytes =  mbraceFTP.Retrieve_Data_From_Server(fileName);
        mbraceFTP.disconnect_from_server();
        publishProgress(1);
        return bytes;
    }

    @Override
    protected void onProgressUpdate(Integer... values) {
        super.onProgressUpdate(values);
        progress_count += values[0];
        UpdateProgress.update_progress_bar(progressBar, progress_count,number_of_tasks);
    }

    @Override
    protected void onPostExecute(char[] bytes) {
        super.onPostExecute(bytes);
        textView.post(new Runnable(){
            @Override
            public void run() {
                textView.setText("Done!");
            }
        });
        if(bytes != null)
        {
            RetrieveDataActivity.data = bytes;
            Intent change_to_graph_data = new Intent(context, GraphActivity.class);
            context.startActivity(change_to_graph_data);
            ((Activity)context).finish();
        }
        else
        {
            UpdateProgress.update_progress_text(textView,"Unable To Get Any Data. Check Connection.");
        }
    }
}
