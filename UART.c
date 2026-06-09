/*
 * uart.c
 *
 *  Created on: Jun 4, 2026
 *      Author: aminashamraze
 *
 *      This file contains all the functions needed to parse commands, this script is suitable
 *      for commands of type
 *      @PWM=15\n
 *
 *
 *      Important:
 const char *data;        // pointer can move, pointed data cannot be modified
char *const data;        // pointer cannot move, pointed data can be modified
const char *const data;  // pointer cannot move, pointed data cannot be modified
 */
#include <stdio.h>
#include <stdbool.h>
#include <strings.h>


#define CMD_BUFFER_SIZE 16
//static will limit the visibility of this global variable only to this file
static int cmd_count =0;
static char cmd_buffer[CMD_BUFFER_SIZE + 1]; //+1 to account for '/n'
//Enums give meaning, that boolean's dont
typedef enum ParserStatus{
    PARSER_WAITING,
    PARSER_COMMAND_READY,
    PARSER_BAD_CHAR,
    PARSER_TOO_LONG
}ParserStatus;

//Store Uart commands in a meaningful struct with char array and integer array

typedef struct UART_finalBuff
{
	uint32_t value;
	char command[20];
}UART_finalBuff;
//UART sends commands in each character, and we will call this function to process/validate that character
bool ProcessRxByte(char c)
{
	if (c == '@')
	{
		return true;
	}
	else if ((c >='0') && (c <='9'))
		{
		return true;
		}
	else if ( (c>='A') && (c <= 'Z'))
		{
		return true;
		}
	else if (c == '=')
	{
		return true;

	}
	else if ((c == '\0') || (c == '\n'))
	{
		return true;
	}
	return false;
}


//It is imp to store characters in a meaningful cmd_buffer, so C commands will enter here.

bool storeBytesInCommands(char *cmd_buffer, char c)
{
	if(cmd_buffer == NULL)
	{
		return false;
	}

	if ( ProcessRxByte(c) != true)
	{
		return false;
	}
	if ( c == '\n')
	{
		cmd_buffer[cmd_count] = 0;
		cmd_count =0;
		return true;
	}
	if (cmd_count >= CMD_BUFFER_SIZE)
	{
		cmd_buffer[cmd_count] = 0;
		cmd_count =0;
		return false;
	}
	else if (cmd_count<CMD_BUFFER_SIZE)
	{
		cmd_buffer[cmd_count] = c;
		cmd_count++;
		return false;
	}

}
//once commands have been stored, we need to use this to update ParserStatus that will be returned to user, in
//a meaningful way

ParserStatus Parser_PushByte(char c)
{
    // validate byte
	if(ProcessRxByte(c)!= true)
	{
		return PARSER_BAD_CHAR;
	}

    // handle '\r'
	if(c == '\r')
	{
		return PARSER_WAITING;
	}
    // handle '\n'
	if(c == '\n')
		{
	    cmd_buffer[cmd_count] = '\0';
	    cmd_count = 0;
	    return PARSER_COMMAND_READY;
		}
    // check length
	if(cmd_count >=CMD_BUFFER_SIZE)
	{
		return PARSER_TOO_LONG;
	}
    // store byte
	if(cmd_count < CMD_BUFFER_SIZE)
	{
		storeBytesInCommands(cmd_buffer, c);
		return PARSER_WAITING;
	}
    // return waiting

}

//if frame is valid,
bool UART_IsValidFrame(char *buf)
	{// checks: starts with '@', contains '=', ends with '\n' or '\0'
	// returns false early so parser never sees garbage
	int contains_eq=0;
	if (*buf != '@')
	{
		return false;
	}
	else
	{
		buf++;
	while((*buf != '\0') && (*buf != '\n'))
	{
	 if(*buf == '=')
	 {
		 return true;
	 }

	 buf++;
	}
	return false;
	}
	}

bool UART_TokenizeCmd(char *raw, char *cmd_name, char *cmd_value)
{// input:  "@PWM=15"
// output: cmd_name="PWM", cmd_value="15"
// strips '@', splits on '
	if((raw == NULL) || (cmd_name == NULL) || (cmd_value == NULL))
	{
		return false;
	}
	if(UART_IsValidFrame(raw) != true)
	{
		return false;
	}
	else
	{
		raw++;
		{
		while(*raw != '=')
		{
			*cmd_name++= *raw++;

		}
		*cmd_name = '\0';
		raw++;
		}
			while((*raw != '\0') && (*raw != '\n'))
			{
				*cmd_value++ = *raw++;
			}
			*cmd_value = '\0';

		return true;
	}

}

//if needed extract integer value from the UART_cmd buffer.
int32_t UART_ParseInt(char *val_str, bool *ok)
{
// converts "15" → 15
	//first i need to strip
// sets ok=false if non-numeric, overflow, etc.
// you can also make UART_ParseFloat() if needed
	int32_t digit, digit_count =0;
	int32_t number=0;
	if(ok == NULL)
	{
		return nnumber;
	}

	*ok = false;

	if(( val_str == NULL))
	{
		return number;

	}

	else
	{
		while(*val_str != '\0')
		{
			if (!((*val_str >= '0') && (*val_str <='9')))
			{
				return 0;
			}
			digit_count++;
			if(digit_count >2)
			{
				return 0;
			}

			digit = *val_str - '0';
			number = 10*number + digit;
			val_str ++;
		}
		*ok = true;
		return number;
	}
}

//write a function that stores UART command once it has been fully processed, and integer
//has been extracted.


bool store_UART(UART_finalBuff *out, const char *data, uint32_t value)
{
    size_t i = 0;

    if ((out == NULL) || (data == NULL))
    {
        return false;
    }

    if (*data != '@')
    {
        return false;
    }

    data++; // skip '@'

    while ((*data != '=') && (*data != '\0') && (*data != '\n'))
    {
        if (i >= UART_COMMAND_SIZE - 1)
        {
            return false;
        }

        out->command[i] = *data;
        i++;
        data++;
    }

    if (*data != '=')
    {
        return false;
    }

    if (i == 0)
    {
        return false; // empty command name, like "@=15"
    }

    out->command[i] = '\0';
    out->value = value;

    return true;
}
    return true;
}
