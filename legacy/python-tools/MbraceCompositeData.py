'''
Created on Aug 18, 2019

@author: Curtis
'''

import ftplib
import os
import datetime
import sys
from gc import collect

main_directory_name = "Mbrace Composite Data"

def get_main_directory_path():
    return os.getcwd() + '/' + main_directory_name

def get_mac_address_raw(file):
    f = file.split(".")
    m = f[0].split("_")    
    if len(m) < 2: return None
    mac_address = m[0]
    mac_components = mac_address.split(":")
    if len(mac_components) != 6: return None
    return mac_address

def get_mac_address_safe(file):
    f = file.split(".")
    m = f[0].split("_")    
    if len(m) < 7: return None
    date_string = m[6]
    split_date = date_string.split("-")
    if len(split_date) != 3: return None   
    mac_address = m[0]
    for i in range(1,6):
        mac_address = mac_address + '_' + m[i]   
    return mac_address

def get_date_string_safe(file):
    f = file.split(".")
    d = f[0].split("_")    
    if len(d) != 7: return None
    date_string = d[6]
    split_date = date_string.split("-")
    if len(split_date) != 3: return None   
    return date_string    
    
def create_directory_path_name(mac_address):
    mac_address_safe = mac_address.replace(':', '_')
    path = os.getcwd() + '/' + main_directory_name + '/' + mac_address_safe
    return path


def collect_data_from_server():
    #open connection to mbrace.xyz ftp server and get list of files
    print('Connecting to ftp.mbrace.xyz')
    file_list = []
    try:
        ftp = ftplib.FTP('ftp.mbrace.xyz')
        ftp.login('u305816916.datacollector','getdata')   
        file_list = ftp.nlst()      
    except ftplib.all_errors as e:
        print(e)
    
    #get a list of all the binary files from the list of files
    bin_list = []
    for file in file_list:
        
        if file.endswith('.bin'):
            bin_list.append(file)
    
    #create a list of mac addresses that start each experiment file
    mac_address_list = []
    for file in bin_list:
            mac_address = get_mac_address_raw(file)
            if mac_address is None : continue
            if mac_address not in mac_address_list:
                mac_address_list.append(mac_address)
    main_path = get_main_directory_path()
    
    if not os.path.isdir(main_path):             
            try:
                os.mkdir(main_path)
            except OSError:
                print ("Creation of the directory %s failed" % main_path)
            else:
                print ("Successfully created the directory %s " % main_path)
    else:
        print(main_path + ' already exists')
             
    print('Setting up directories..')
    #make a directory for each mac address found if it doesnt exist
    #and save the paths in a list
    composite_dir_path_list = []         
    for mac_address in mac_address_list:    
        path = create_directory_path_name(mac_address)
        composite_dir_path_list.append(path)
        if not os.path.isdir(path):             
            try:
                os.mkdir(path)
            except OSError:
                print ("Creation of the directory %s failed" % path)
            else:
                print ("Successfully created the directory %s " % path)
        else:
            print(path + ' already exists')
    
    #copy experiments files from the ftp server and      
    #save each experiment file in the directory with the same name as its mac address
    print('Downloading files..')
    for file in bin_list:
        mac_address = get_mac_address_raw(file)
        if mac_address is None : continue
        path = create_directory_path_name(mac_address)
        if path in composite_dir_path_list:
            local_file_name = file.replace(':', '_')
            #check date of file and exclude if it's the same as the current date
            date_string = get_date_string_safe(local_file_name)
            if date_string is None: continue
            datetime_obj = datetime.datetime.strptime(date_string, '%Y-%m-%d')
            now_datetime = datetime.datetime.now()
            if datetime_obj.date() == now_datetime.date(): 
                print('Excluding ' + local_file_name + ' because it is possibly an active experiment.')
            local_file_path = path + '/' + local_file_name
            
            if not os.path.isfile(local_file_path):
                print('    Downloading ' + file + '..')
                local_bin_file = open(local_file_path, 'wb+')
                ftp.retrbinary('RETR ' + file, local_bin_file.write)
                local_bin_file.close()
    ftp.close()
    print('Download complete!')

def create_composites(acceptable_days_apart):
    composite_dir_list = os.listdir(get_main_directory_path())
    main_path = get_main_directory_path()
    composite_path_list = []
    for directory in composite_dir_list:
        path = main_path + '/' + directory
        if not os.path.isdir(path): continue       
        composite_path_list.append(path)
        
        
    for path in composite_path_list:
        
        #create a sorted dictionary where keys are the date
        #and values are the filenames
        file_list = os.listdir(path) 
        file_date_dict = {}
        for file in file_list:
            if not os.path.isfile(path + '/' + file): continue
            if 'Composite' in file:
                print('Removing ' + file)
                os.remove(path + '/' + file)
                file_list.remove(file)
                continue
            date_string = get_date_string_safe(file)
            if date_string is None: continue
            datetime_obj = datetime.datetime.strptime(date_string, '%Y-%m-%d')
            file_date_dict[datetime_obj.date()] = file
        #create a list representing the keys as sorted dates
        sorted_date_list = []
        for key in sorted(file_date_dict.keys()):
            sorted_date_list.append(key)
            
        date_group_list = []
        date_group_index = 0
        date_group_list.append([])
        current_date = sorted_date_list[0]
        for date in sorted_date_list:
            time_diff = date - current_date
            current_date = date
            if time_diff.days <= acceptable_days_apart:
                date_group_list[date_group_index].append(date)
            else:
                date_group_index = date_group_index + 1
                date_group_list.append([])
                date_group_list[date_group_index].append(date)
        mac_address_safe = get_mac_address_safe(file_list[0])
        date_string_format = '%Y-%m-%d'
        for group in date_group_list:
            print('Compositing for ' + mac_address_safe + '..')
            start_date = group[0].strftime(date_string_format)
            end_date = group[-1].strftime(date_string_format)
            group_file_name = mac_address_safe + "_Composite_" + start_date + '_to_' + end_date
            group_file_path = path + '/' + group_file_name
            
           
            group_file = open(group_file_path, 'ab+')
            print(' Combining data for ' + group_file_name)
            for date in group:
                print('    adding data from ' + file_date_dict[date])
                file_to_copy_path = path + "/" + file_date_dict[date]
                file_to_copy = open(file_to_copy_path, 'rb')
                group_file.write(file_to_copy.read())
                file_to_copy.close()
            group_file.close()
            print('Completed!')
    print('Composite creation complete! Files were composited if they they are within ' + str(acceptable_days_apart) + ' day(s) apart.')
           
if __name__ == "__main__":
    collect_data_from_server()
    create_composites(1)
