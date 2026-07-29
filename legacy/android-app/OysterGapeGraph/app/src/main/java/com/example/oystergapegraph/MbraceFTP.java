package com.example.oystergapegraph;

import android.util.Log;
import android.widget.ProgressBar;

import org.apache.commons.io.IOUtils;
import org.apache.commons.net.ftp.FTP;
import org.apache.commons.net.ftp.FTPClient;
import org.apache.commons.net.ftp.FTPReply;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.SortedMap;
import java.util.TreeMap;
import java.util.regex.Pattern;

public class MbraceFTP {
    public static final FTPClient ftp_client = new FTPClient();

    private static final String TAG = "MbraceFTP";

    public char[] Retrieve_Data_From_Server(String file_name)
    {
        try
        {
            InputStream file_data_stream = ftp_client.retrieveFileStream(file_name);
            byte[] signed_byte_array = IOUtils.toByteArray(file_data_stream);
            char[] uint16_array = new char[signed_byte_array.length];
            for(int i = 0; i < signed_byte_array.length; i++)
            {
                uint16_array[i] = signed_byte_array[i] < 0 ? (char)(signed_byte_array[i] & 0xFF) : (char)(signed_byte_array[i]);
            }
            return uint16_array;
        }
        catch(IOException e)
        {
            Log.e(TAG, "Error retrieving bytes of data.", e);
        }
        return null;
    }

    public List<String> Get_Bin_Data_Files()
    {
        String[] all_files;
        try{
            all_files = ftp_client.listNames();
        }
        catch(IOException e)
        {
            Log.e(TAG, "Error retrieving list of files.", e);
            return null;
        }
        SortedMap<String, String> sorted_bin_map = new TreeMap<>(Collections.<String>reverseOrder());
        for (String file : all_files) {
            if(!file.contains(".bin")) continue;
            String date = get_date_string(file);
            if (date == null) continue;
            sorted_bin_map.put(date, file);
        }
        List<String> bin_file_names = new ArrayList<>();
        for(Map.Entry<String,String> bin : sorted_bin_map.entrySet())
        {
            bin_file_names.add(bin.getValue());
        }
        return bin_file_names;
    }

    private String get_date_string(String file) {
        Log.d(TAG,file);
        String[] f = file.split(Pattern.quote("."));
        String[] d = f[0].split(Pattern.quote("_"));
        if(d.length < 2) return null;
        String date = d[1];
        String[] split_date = date.split(Pattern.quote("-"));
        if(split_date.length < 3) return null;
        return date;
    }

    public boolean connect_to_server()
    {
        if(ftp_client.isConnected())
        {
            return true;
        }
        String server_name = "ftp.mbrace.xyz";
        String user_name = "u305816916.datacollector";
        String password = "getdata";
        int port = 21;
        try
        {
            ftp_client.connect(server_name, port);
            ftp_client.login(user_name, password);
            if (FTPReply.isPositiveCompletion((ftp_client.getReplyCode())))
            {
                ftp_client.enterLocalPassiveMode();
                ftp_client.setFileType(FTP.BINARY_FILE_TYPE);
            }
            return true;
        }
        catch(IOException e)
        {
            Log.e(TAG, "Error setting up FTP connection.", e);
        }
        return false;
    }

    protected boolean disconnect_from_server()
    {
        if(!ftp_client.isConnected()) return true;
        try
        {
            ftp_client.logout();
            ftp_client.disconnect();
            return true;
        } catch (Exception e)
        {
            Log.e(TAG, "Error disconnection from FTP server.", e);
        }
        return false;
    }
}
