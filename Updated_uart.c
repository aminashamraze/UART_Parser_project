#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define UART_FRAME_MAX    16U
#define UART_COMMAND_MAX   8U  // 7  + '\0'
#define UART_VALUE_MAX    11U  /* 10  + '\0' */
/* ---------- Status values ---------- */
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
/* ---------- Supported commands ---------- */
typedef enum
{
    UART_CMD_PWM,
    UART_CMD_LED,
    UART_CMD_RATE,
    UART_CMD_UNKNOWN
} UART_Command;


/* ---------- Parser state ---------- */

typedef struct
{
    /*
     * UART_FRAME_MAX characters plus one byte for '\0'.
     */
    char frame[UART_FRAME_MAX + 1U];
    size_t length;
    /*
     * If the current frame becomes invalid, ignore all bytes
     * until '\n' arrives.
     */
    bool discarding;
} UART_Parser;
/* ---------- Tokenized text ---------- */

typedef struct
{
    char command[UART_COMMAND_MAX];
    char value[UART_VALUE_MAX];
} UART_Tokens;


/* ---------- Interpreted command ---------- */

typedef struct
{
    UART_Command command;
    uint32_t value;
} UART_Message;


* Parser state management*/
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


/*
 * Public initialization function.
 */
void UART_ParserInit(UART_Parser *parser)
{
    UART_ParserReset(parser);
}


/* =========================================================
 * Individual-byte validation
 * ========================================================= */

static bool UART_IsAllowedByte(char c)
{
    if ((c >= 'A') && (c <= 'Z'))
    {
        return true;
    }
    else if ((c >= '0') && (c <= '9'))
    {
        return true;
    }
    else if ((c == '@') || (c == '='))
    {
        return true;
    }
    else
    {
        return false;
    }
}


/* 
 * Complete-frame validation */

static bool UART_IsValidFrame(const char *frame)
{
    size_t index = 0U;
    size_t equals_count = 0U;
    size_t equals_index = 0U;

    if (frame == NULL)
    {
        return false;
    }

    /*
     * Expected format:
     * @COMMAND=VALUE
     */
    if (frame[0] != '@')
    {
        return false;
    }

    /*
     * Begin checking after the first '@'.
     */
    index = 1U;

    while (frame[index] != '\0')
    {
        /*
         * '@' is allowed only at the beginning.
         */
        if (frame[index] == '@')
        {
            return false;
        }

        if (frame[index] == '=')
        {
            equals_count++;
            equals_index = index;
        }

        index++;
    }

    /*
     * There must be exactly one '='.
     */
    if (equals_count != 1U)
    {
        return false;
    }

    /* Reject an empty command: @=15
     */
    if (equals_index == 1U)
    {
        return false;
    }

    /*
     * Reject an empty value:
     *
     * @PWM=
     */
    if (frame[equals_index + 1U] == '\0')
    {
        return false;
    }

    return true;
}


/*
 * Tokenization */

static bool UART_TokenizeFrame(
    const char *frame,
    UART_Tokens *tokens_out)
{
    size_t frame_index = 1U; /* Skip '@'. */
    size_t command_index = 0U;
    size_t value_index = 0U;

    if ((frame == NULL) || (tokens_out == NULL))
    {
        return false;
    }

    /*
     * Copy command characters until '='.
     */
    while (frame[frame_index] != '=')
    {
        /*
         * Defensive check in case this function receives an
         * unvalidated frame.
         */
        if (frame[frame_index] == '\0')
        {
            return false;
        }

        /*
         * Leave one byte available for '\0'.
         */
        if (command_index >= UART_COMMAND_MAX - 1U)
        {
            return false;
        }

        tokens_out->command[command_index] =
            frame[frame_index];

        command_index++;
        frame_index++;
    }

    tokens_out->command[command_index] = '\0';

    /*
     * Skip '='.
     */
    frame_index++;

    /*
     * Copy value characters until the end of the frame.
     */
    while (frame[frame_index] != '\0')
    {
        /*
         * A second '=' should not appear in the value.
         */
        if (frame[frame_index] == '=')
        {
            return false;
        }

        /*
         * Leave one byte available for '\0'.
         */
        if (value_index >= UART_VALUE_MAX - 1U)
        {
            return false;
        }

        tokens_out->value[value_index] =
            frame[frame_index];

        value_index++;
        frame_index++;
    }

    tokens_out->value[value_index] = '\0';

    return true;
}


/*
 * Complete-frame parsing
 * This stage validates and tokenizes the completed frame.
 * It does not yet convert the command or value.*/

static ParserStatus UART_ParseFrame(
    const char *frame,
    UART_Tokens *tokens_out)
{
    if ((frame == NULL) || (tokens_out == NULL))
    {
        return PARSER_INVALID_ARGUMENT;
    }
    /*
     * Stage 1: validate the complete frame.
     */
    if (!UART_IsValidFrame(frame))
    {
        return PARSER_BAD_FRAME;
    }

    /*
     * Stage 2: split the valid frame into two strings.
     */
    if (!UART_TokenizeFrame(frame, tokens_out))
    {
        return PARSER_BAD_FRAME;
    }

    return PARSER_COMMAND_READY;
}


/* =========================================================
 * Byte-by-byte frame collection
 * ========================================================= */

ParserStatus UART_ParserPushByte(
    UART_Parser *parser,
    char c,
    UART_Tokens *tokens_out)
{
    ParserStatus result;

    if ((parser == NULL) || (tokens_out == NULL))
    {
        return PARSER_INVALID_ARGUMENT;
    }

    /*
     * If the current frame was already invalid, discard all
     * remaining bytes until newline.
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
     * Ignore carriage return so both "\n" and "\r\n" work.
     */
    if (c == '\r')
    {
        return PARSER_WAITING;
    }

    /*
     * Newline completes the current frame.
     */
    if (c == '\n')
    {
        if (parser->length == 0U)
        {
            UART_ParserReset(parser);
            return PARSER_BAD_FRAME;
        }

        /*
         * Turn the collected bytes into a C string.
         */
        parser->frame[parser->length] = '\0';

        result = UART_ParseFrame(
            parser->frame,
            tokens_out);

        /*
         * Prepare for the next frame.
         */
        UART_ParserReset(parser);

        return result;
    }

    /*
     * Reject unsupported characters.
     */
    if (!UART_IsAllowedByte(c))
    {
        parser->length = 0U;
        parser->frame[0] = '\0';
        parser->discarding = true;

        return PARSER_BAD_CHAR;
    }

    /*
     * frame[] can hold UART_FRAME_MAX characters.
     * The additional array byte is reserved for '\0'.
     */
    if (parser->length >= UART_FRAME_MAX)
    {
        parser->length = 0U;
        parser->frame[0] = '\0';
        parser->discarding = true;

        return PARSER_TOO_LONG;
    }

    /*
     * Store the byte and move to the next position.
     */
    parser->frame[parser->length] = c;
    parser->length++;

    return PARSER_WAITING;
}


/* =========================================================
 * Command string to enum
 * ========================================================= */

static UART_Command UART_CommandFromString(
    const char *command_string)
{
    if (command_string == NULL)
    {
        return UART_CMD_UNKNOWN;
    }

    if (strcmp(command_string, "PWM") == 0)
    {
        return UART_CMD_PWM;
    }
    else if (strcmp(command_string, "LED") == 0)
    {
        return UART_CMD_LED;
    }
    else if (strcmp(command_string, "RATE") == 0)
    {
        return UART_CMD_RATE;
    }
    else
    {
        return UART_CMD_UNKNOWN;
    }
}


/* =========================================================
 * Value string to uint32_t
 * ========================================================= */

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
     * Reject an empty value.
     */
    if (*value_string == '\0')
    {
        return false;
    }

    while (*value_string != '\0')
    {
        uint32_t digit;

        if ((*value_string < '0') ||
            (*value_string > '9'))
        {
            return false;
        }

        digit = (uint32_t)(*value_string - '0');

        /*
         * Check whether:
         *
         * value * 10 + digit
         *
         * would overflow uint32_t.
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


/* =========================================================
 * Interpret tokens
 *
 * command string → enum
 * value string   → integer
 * ========================================================= */

static ParserStatus UART_InterpretTokens(
    const UART_Tokens *tokens,
    UART_Message *message_out)
{
    UART_Command command;
    uint32_t value;

    if ((tokens == NULL) || (message_out == NULL))
    {
        return PARSER_INVALID_ARGUMENT;
    }

    command =
        UART_CommandFromString(tokens->command);

    if (command == UART_CMD_UNKNOWN)
    {
        return PARSER_UNKNOWN_COMMAND;
    }

    if (!UART_ParseUint32(tokens->value, &value))
    {
        return PARSER_BAD_VALUE;
    }

    message_out->command = command;
    message_out->value = value;

    return PARSER_COMMAND_READY;
}


/* =========================================================
 * Command handlers
 * ========================================================= */

static bool UART_HandlePWM(uint32_t value)
{
    /*
     * Example duty-cycle range: 0–100%.
     */
    if (value > 100U)
    {
        return false;
    }

    /*
     * Actual embedded implementation:
     *
     * PWM_SetDutyCycle(value);
     */

    return true;
}


static bool UART_HandleLED(uint32_t value)
{
    /*
     * LED accepts only 0 or 1.
     */
    if (value > 1U)
    {
        return false;
    }

    /*
     * Actual embedded implementation:
     *
     * LED_Set(value != 0U);
     */

    return true;
}


static bool UART_HandleRate(uint32_t value)
{
    /*
     * Example allowed rate: 1–1000.
     */
    if ((value == 0U) || (value > 1000U))
    {
        return false;
    }

    /*
     * Actual embedded implementation:
     *
     * SamplingRate_Set(value);
     */

    return true;
}


/* =========================================================
 * Command dispatch
 * ========================================================= */

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


/* =========================================================
 * Example application-level use
 * ========================================================= */

void UART_ProcessReceivedByte(
    UART_Parser *parser,
    char received_byte)
{
    UART_Tokens tokens;
    UART_Message message;
    ParserStatus status;

    /*
     * First collect, validate, and tokenize the frame.
     */
    status = UART_ParserPushByte(
        parser,
        received_byte,
        &tokens);

    if (status != PARSER_COMMAND_READY)
    {
        /*
         * The command is incomplete or an error occurred.
         * The real application could log or count errors here.
         */
        return;
    }

    /*
     * Next convert strings into typed values.
     */
    status = UART_InterpretTokens(
        &tokens,
        &message);

    if (status != PARSER_COMMAND_READY)
    {
        /*
         * Unknown command or invalid numeric value.
         */
        return;
    }

    /*
     * Finally execute the command.
     */
    (void)UART_Dispatch(&message);
}
