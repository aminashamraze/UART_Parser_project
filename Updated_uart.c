#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UART_FRAME_MAX 16U

typedef enum
{
    PARSER_WAITING,
    PARSER_COMMAND_READY,
    PARSER_BAD_CHAR,
    PARSER_TOO_LONG,
    PARSER_BAD_FRAME,
    PARSER_BAD_VALUE,
    PARSER_UNKNOWN_COMMAND,
    PARSER_INVALID_ARGUMENT
} ParserStatus;

typedef enum
{
    UART_CMD_PWM,
    UART_CMD_LED,
    UART_CMD_RATE,
    UART_CMD_UNKNOWN
} UART_Command;

typedef struct
{
    char frame[UART_FRAME_MAX + 1U];
    size_t length;

    /*
     * When an invalid or oversized frame is detected, ignore bytes
     * until the next newline so the parser can resynchronize.
     */
    bool discarding;
} UART_Parser;

typedef struct
{
    UART_Command command;
    uint32_t value;
} UART_Message;


/* ---------- Parser state management ---------- */

static void UART_ParserReset(UART_Parser *parser)
{
    if (parser == NULL)
    {
        return;
    }

    parser->length = 0U;
    parser->discarding = false;
    parser->frame[0] = '\0';
}

void UART_ParserInit(UART_Parser *parser)
{
    UART_ParserReset(parser);
}


/* ---------- Individual-byte validation ---------- */

static bool UART_IsAllowedByte(char c)
{
    if ((c >= 'A') && (c <= 'Z'))
    {
        return true;
    }

    if ((c >= '0') && (c <= '9'))
    {
        return true;
    }

    return (c == '@') || (c == '=');
}


/* ---------- Command string to enum ---------- */

static UART_Command UART_CommandFromString(
    const char *name,
    size_t length)
{
    if (name == NULL)
    {
        return UART_CMD_UNKNOWN;
    }

    if ((length == 3U) && (memcmp(name, "PWM", 3U) == 0))
    {
        return UART_CMD_PWM;
    }

    if ((length == 3U) && (memcmp(name, "LED", 3U) == 0))
    {
        return UART_CMD_LED;
    }

    if ((length == 4U) && (memcmp(name, "RATE", 4U) == 0))
    {
        return UART_CMD_RATE;
    }

    return UART_CMD_UNKNOWN;
}


/* ---------- Safe unsigned integer conversion ---------- */

static bool UART_ParseUint32(
    const char *value_string,
    uint32_t *value_out)
{
    uint32_t value = 0U;

    if ((value_string == NULL) || (value_out == NULL))
    {
        return false;
    }

    /*
     * Reject an empty value, such as "@PWM=".
     */
    if (*value_string == '\0')
    {
        return false;
    }

    while (*value_string != '\0')
    {
        uint32_t digit;

        if ((*value_string < '0') || (*value_string > '9'))
        {
            return false;
        }

        digit = (uint32_t)(*value_string - '0');

        /*
         * Check before calculating:
         *
         * value = value * 10 + digit
         */
        if (value > ((UINT32_MAX - digit) / 10U))
        {
            return false;
        }

        value = (value * 10U) + digit;
        value_string++;
    }

    *value_out = value;
    return true;
}


/* ---------- Complete-frame parsing ---------- */

static ParserStatus UART_ParseFrame(
    const char *frame,
    UART_Message *message_out)
{
    const char *equals;
    const char *command_start;
    const char *value_start;
    size_t command_length;
    UART_Command command;
    uint32_t value;

    if ((frame == NULL) || (message_out == NULL))
    {
        return PARSER_INVALID_ARGUMENT;
    }

    /*
     * Expected format:
     *
     * @COMMAND=VALUE
     */
    if (frame[0] != '@')
    {
        return PARSER_BAD_FRAME;
    }

    command_start = &frame[1];
    equals = strchr(command_start, '=');

    if (equals == NULL)
    {
        return PARSER_BAD_FRAME;
    }

    /*
     * Reject an empty command name: "@=15".
     */
    if (equals == command_start)
    {
        return PARSER_BAD_FRAME;
    }

    value_start = equals + 1;

    /*
     * Reject an empty value: "@PWM=".
     */
    if (*value_start == '\0')
    {
        return PARSER_BAD_FRAME;
    }

    /*
     * Reject another '=' inside the value.
     */
    if (strchr(value_start, '=') != NULL)
    {
        return PARSER_BAD_FRAME;
    }

    command_length = (size_t)(equals - command_start);

    command = UART_CommandFromString(
        command_start,
        command_length);

    if (command == UART_CMD_UNKNOWN)
    {
        return PARSER_UNKNOWN_COMMAND;
    }

    if (!UART_ParseUint32(value_start, &value))
    {
        return PARSER_BAD_VALUE;
    }

    message_out->command = command;
    message_out->value = value;

    return PARSER_COMMAND_READY;
}


/* ---------- Main byte-by-byte parser ---------- */

ParserStatus UART_ParserPushByte(
    UART_Parser *parser,
    char c,
    UART_Message *message_out)
{
    ParserStatus result;

    if ((parser == NULL) || (message_out == NULL))
    {
        return PARSER_INVALID_ARGUMENT;
    }

    /*
     * Support CRLF input.
     * Ignore '\r' and treat '\n' as the frame terminator.
     */
    if (c == '\r')
    {
        return PARSER_WAITING;
    }

    /*
     * If a previous byte made the frame invalid, discard bytes until
     * newline so the next command starts from a known boundary.
     */
    if (parser->discarding)
    {
        if (c == '\n')
        {
            UART_ParserReset(parser);
        }

        return PARSER_WAITING;
    }

    /*
     * Newline completes the frame.
     */
    if (c == '\n')
    {
        if (parser->length == 0U)
        {
            UART_ParserReset(parser);
            return PARSER_BAD_FRAME;
        }

        parser->frame[parser->length] = '\0';

        result = UART_ParseFrame(
            parser->frame,
            message_out);

        UART_ParserReset(parser);
        return result;
    }

    if (!UART_IsAllowedByte(c))
    {
        parser->length = 0U;
        parser->frame[0] = '\0';
        parser->discarding = true;

        return PARSER_BAD_CHAR;
    }

    /*
     * frame[] has UART_FRAME_MAX + 1 bytes.
     * Therefore, it can hold UART_FRAME_MAX command characters
     * plus the terminating '\0'.
     */
    if (parser->length >= UART_FRAME_MAX)
    {
        parser->length = 0U;
        parser->frame[0] = '\0';
        parser->discarding = true;

        return PARSER_TOO_LONG;
    }

    parser->frame[parser->length] = c;
    parser->length++;

    return PARSER_WAITING;
}


/* ---------- Command dispatch ---------- */

/*
 * Replace these example handlers with actual hardware-control functions.
 */

static bool UART_HandlePWM(uint32_t value)
{
    /*
     * Example PWM range: 0–100%.
     */
    if (value > 100U)
    {
        return false;
    }

    /* PWM_SetDutyCycle(value); */
    return true;
}

static bool UART_HandleLED(uint32_t value)
{
    if (value > 1U)
    {
        return false;
    }

    /* LED_Set(value != 0U); */
    return true;
}

static bool UART_HandleRate(uint32_t value)
{
    if ((value == 0U) || (value > 1000U))
    {
        return false;
    }

    /* SamplingRate_Set(value); */
    return true;
}

bool UART_Dispatch(const UART_Message *message)
{
    if (message == NULL)
    {
        return false;
    }

    switch (message->command)
    {
        case UART_CMD_PWM:
            return UART_HandlePWM(message->value);

        case UART_CMD_LED:
            return UART_HandleLED(message->value);

        case UART_CMD_RATE:
            return UART_HandleRate(message->value);

        case UART_CMD_UNKNOWN:
        default:
            return false;
    }
}
