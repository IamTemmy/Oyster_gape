/* Written by James Curtis Addy*/
const char* base64_table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64_encode(char* output_string, char* input_string, int input_length)
{
	char bytes_3[3] = "";
	char bytes_4[4] = "";
	int padding = 0;	
	int output_index = 0;
	
	for(int i = 0; i < input_length; i += 3)
	{
		int bytes_count = 0;
		char bytes_3[3] = "";
		char bytes_4[4] = "";
		for(int j = 0; j < 3 && i + j < input_length; j++)
		{
			bytes_3[j] = input_string[i + j];
			bytes_count += 1;
		}
		
	    padding = 3 - bytes_count;
		
		bytes_4[0] = (bytes_3[0] >> 2) & 63;
		bytes_4[1] = ((bytes_3[0] & 3) << 4) | ((bytes_3[1] >> 4) & 15);
		bytes_4[2] = ((bytes_3[1] & 15) << 2) | ((bytes_3[2] >> 6) & 3);
		bytes_4[3] = bytes_3[2] & 63;
		
		for(int k = 0; k < (4-padding); k++)
		{
			output_string[output_index] = base64_table[bytes_4[k]];
			output_index += 1;
		}
 	}
	
	for(int i = 0; i < padding; i++)
	{
		output_string[output_index] = '=';
		output_index += 1;
	}
	
	output_string[output_index] = '\0';
	return output_index;
}

int base64_encoded_length(int length)
{
	int triplets = (length + 2) / 3;
	return (triplets * 4) + 1;
}