package com.example.oystergapegraph;

import android.content.Intent;
import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.Spinner;

import java.util.List;

public class FileSelectActivity extends AppCompatActivity implements AdapterView.OnItemSelectedListener {
    Spinner dropDown;
    List<String> filenames;
    Button select_button;
    String selection;
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_file_select);
        select_button = findViewById(R.id.SelectFile);
        filenames = getIntent().getStringArrayListExtra("FILENAMES");
        dropDown = findViewById(R.id.spinner);
        dropDown.setOnItemSelectedListener(this);
        ArrayAdapter<String> listAdapter = new ArrayAdapter<>(this,android.R.layout.simple_spinner_item, filenames);
        listAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        dropDown.setAdapter(listAdapter);
        select_button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent change_to_acquire_data = new Intent(FileSelectActivity.this,RetrieveDataActivity.class);
                change_to_acquire_data.putExtra("GRAPHFILE", selection);
                startActivity(change_to_acquire_data);
            }
        });
    }

    @Override
    public void onItemSelected(AdapterView<?> parent, View view, int position, long id) {
        String file_choice = parent.getItemAtPosition(position).toString();
        selection = file_choice;
    }

    @Override
    public void onNothingSelected(AdapterView<?> parent) {

    }
}
