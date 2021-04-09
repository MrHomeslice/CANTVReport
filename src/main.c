#include <stdbool.h>
#include <stdio.h>
#include <jansson.h>
#include <glib.h>
#include <argp.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <curl/multi.h>
#include <strings.h>
#include <libgen.h>
#include <sys/stat.h>

typedef struct 
{
    const char *pStartDate, *pEndDate, *pAccount, *pAPIKey, *pEmailFrom, *pEmailTo, *pEmailFromName, *pEmailPassword;
} CmdLineArgs;

CmdLineArgs g_cmdArgs;

int asprintf(char **str, const char* fmt, ...)
{
   va_list argp;
   va_start(argp, fmt);

   char one_char[1];

   int len = vsnprintf(one_char, 1, fmt, argp);

   va_end(argp);

   if (len < 1)
   {
      fprintf(stderr, "Error formatting string in asprintf\n");
      
      *str = NULL;
      return len;
   }

   *str = malloc(len+1);

   if (!(*str)) 
   {
      fprintf(stderr, "Failure allocating %d bytes in asprintf\n", len+1);

      return -1;
   }

   va_start(argp, fmt);
   vsnprintf(*str, len+1, fmt, argp);
   va_end(argp);

   return len;
}

void FreeString(char **pString)
{
   if (*pString != NULL)
   {
      free(*pString);

      *pString = NULL;
   }
}

struct string 
{
    char   *pCharData;
    size_t dataLen;
};

void InitResponseString(struct string *s) 
{
    s->dataLen = 0;
    s->pCharData = malloc(s->dataLen + 1);

    if (s->pCharData == NULL) 
    {
        fprintf(stderr, "Failure allocating memory in InitResponseString\n");
        return;
    }
    
    s->pCharData[0] = '\0';
}

size_t ResponseWrite(void *ptr, size_t size, size_t nmemb, struct string *s)
{
    size_t new_len = s->dataLen + size*nmemb;
    
    s->pCharData = realloc(s->pCharData, new_len + 1);
    
    if (s->pCharData == NULL) 
    {
        fprintf(stderr, "realloc() failed\n");
        exit(EXIT_FAILURE);
    }
    
    memcpy(s->pCharData+s->dataLen, ptr, size*nmemb);
    
    s->pCharData[new_len] = '\0';
    s->dataLen = new_len;

    return size * nmemb;
}

static char *decoding_table = NULL;

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
                                'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
                                'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
                                'Y', 'Z', 'a', 'b', 'c', 'd', 'e', 'f',
                                'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n',
                                'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
                                'w', 'x', 'y', 'z', '0', '1', '2', '3',
                                '4', '5', '6', '7', '8', '9', '+', '/'};

void build_decoding_table() 
{
    decoding_table = malloc(256);

    for (int i = 0; i < 64; i++)
    {
        decoding_table[(unsigned char) encoding_table[i]] = i;
    }
}

void base64_cleanup() 
{
    free(decoding_table);
}

static int mod_table[] = {0, 2, 1};

#define uint32_t unsigned int

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) 
{
    *output_length = 4 * ((input_length + 2) / 3);

    char *encoded_data = malloc(*output_length);

    if (encoded_data == NULL) return NULL;

    for (int i = 0, j = 0; i < input_length;) {

        uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
        uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

        encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
        encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    for (int i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[*output_length - 1 - i] = '=';

    return encoded_data;
}

unsigned char *base64_decode(const char *data,
                             size_t input_length,
                             size_t *output_length) {

    if (decoding_table == NULL) build_decoding_table();

    if (input_length % 4 != 0) return NULL;

    *output_length = input_length / 4 * 3;
    if (data[input_length - 1] == '=') (*output_length)--;
    if (data[input_length - 2] == '=') (*output_length)--;

    unsigned char *decoded_data = malloc(*output_length);
    if (decoded_data == NULL) return NULL;

    for (int i = 0, j = 0; i < input_length;) {

        uint32_t sextet_a = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
        uint32_t sextet_b = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
        uint32_t sextet_c = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];
        uint32_t sextet_d = data[i] == '=' ? 0 & i++ : decoding_table[data[i++]];

        uint32_t triple = (sextet_a << 3 * 6)
        + (sextet_b << 2 * 6)
        + (sextet_c << 1 * 6)
        + (sextet_d << 0 * 6);

        if (j < *output_length) decoded_data[j++] = (triple >> 2 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 1 * 8) & 0xFF;
        if (j < *output_length) decoded_data[j++] = (triple >> 0 * 8) & 0xFF;
    }

    return decoded_data;
}

#ifdef COMMENT_OUT

curl --ssl-reqd \
>   --url 'smtps://smtp.gmail.com:465' \
>   --user 'patrickgraphicssystems@gmail.com:P!ckl3rick' \
>   --mail-from 'patrickgraphicssystems@gmail.com' \
>   --mail-rcpt 'patrickgraphicssystems@example.com' \
>   --upload-file email.txt

#endif

void GetGMTTime(char *pTimeBuffer, int bufferLength)
{
   struct timeval  tv;
   gettimeofday(&tv, NULL);

   struct tm  tstruct;
   
   tstruct = *gmtime(&tv.tv_sec);   

   strftime(pTimeBuffer, bufferLength, "%Y-%m-%d %X", &tstruct);   
}

static const char *s_pPayloadFormat = 
  "Date: %s\r\n"
  "To: %s\r\n"
  "From: %s (%s)\r\n"
  "Subject: CANTV Report\r\n"
  "MIME-Version: 1.0\r\n"
  "Content-Type: multipart/mixed; boundary=\"MULTIPART-MIXED-BOUNDARY\"\r\n"
  "\r\n"
  "CANTV Report is attached\r\n"
  "--MULTIPART-MIXED-BOUNDARY\r\n"
  "Content-Type: text/plain; charset=utf-8\r\n"
  "Content-Transfer-Encoding: base64\r\n"
  "Content-Disposition: attachment; filename=\"report.csv\"\r\n%s";

struct upload_status 
{
    int bytesRemaining;
    char *pBuffer;
};

char *g_pPayloadText = NULL;

size_t g_bytesRemaining = -1;

static size_t payload_source(void *ptr, size_t size, size_t nmemb, void *userp)
{
    struct upload_status *upload_ctx = (struct upload_status *) userp;

    int maxCopy = size * nmemb;

    if ((size == 0) || (nmemb == 0) || (maxCopy < 1) || (upload_ctx->bytesRemaining == 0))
    {
        return 0;
    }

    int bytesToCopy = maxCopy > upload_ctx->bytesRemaining ? upload_ctx->bytesRemaining : maxCopy;

    memcpy(ptr, upload_ctx->pBuffer, bytesToCopy);

    upload_ctx->bytesRemaining -= bytesToCopy;
    upload_ctx->pBuffer        += bytesToCopy;

    return bytesToCopy;
}

int SendEmail(const char *pAttachmentName, const char *pAttachment)
{
    struct upload_status upload_ctx;

    char gmtDate[100];
    
    GetGMTTime(gmtDate, 100);

    char *pSendBuffer;
    
    asprintf(&pSendBuffer, s_pPayloadFormat, gmtDate, g_cmdArgs.pEmailTo, g_cmdArgs.pEmailFrom, g_cmdArgs.pEmailFromName, pAttachment);

    upload_ctx.pBuffer        = pSendBuffer;
    upload_ctx.bytesRemaining = strlen(pSendBuffer) + 1;

    CURL *curl;
    CURLcode res = CURLE_OK;
    struct curl_slist *recipients = NULL;

    curl = curl_easy_init();

    if (curl) 
    {
        curl_easy_setopt(curl, CURLOPT_URL, "smtps://smtp.gmail.com:465");
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
        curl_easy_setopt(curl, CURLOPT_USERNAME, g_cmdArgs.pEmailFrom);
        curl_easy_setopt(curl, CURLOPT_PASSWORD, g_cmdArgs.pEmailPassword);
        curl_easy_setopt(curl, CURLOPT_MAIL_FROM, g_cmdArgs.pEmailFrom);

        recipients = curl_slist_append(recipients, g_cmdArgs.pEmailTo);
        curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);

        curl_easy_setopt(curl, CURLOPT_READFUNCTION, payload_source);
        curl_easy_setopt(curl, CURLOPT_READDATA, &upload_ctx);
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        res = curl_easy_perform(curl);

        if(res != CURLE_OK)
        fprintf(stderr, "curl_easy_perform() failed: %s\n",
                curl_easy_strerror(res));

        curl_slist_free_all(recipients);

        curl_easy_cleanup(curl);

        free(pSendBuffer);
    }

    return (int) res;
}

int GetHTTP(const char *pURL, const char *pUserPass, char **pResponse)
{
    int status = 500;
    
    CURLcode res;

    CURL *pCurl = curl_easy_init();

    *pResponse = NULL;

    if (pCurl) 
    {
        struct string s;

        InitResponseString(&s);

        curl_easy_setopt(pCurl, CURLOPT_URL, pURL);
        
        curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, ResponseWrite);
        
        curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &s);

        curl_easy_setopt(pCurl, CURLOPT_TIMEOUT_MS, 4000L);  

        if (pUserPass)
        {        
            curl_easy_setopt(pCurl, CURLOPT_USERPWD, pUserPass);
        }
        
        res = curl_easy_perform(pCurl);

        if (res == CURLE_OK) 
        {    
            long responseCode = 0;

            res = curl_easy_getinfo(pCurl, CURLINFO_RESPONSE_CODE, &responseCode);

            if (res == CURLE_OK)
            {
                if (responseCode == 200)
                {
                    *pResponse = s.pCharData;
                }

                status = responseCode;
            }
        } 

        if (status != 200)
        {
            free(s.pCharData);
        }               
  
        curl_easy_cleanup(pCurl);
    }

    return status;
}

bool GetJSONString(json_t *pObject, char **pValue, const char *pName)
{
   json_t *pJSONString = json_object_get(pObject, pName);

   if (pJSONString)
   {
      const char *pJSONValue = json_string_value(pJSONString);

      if (pJSONValue)
      {
         *pValue = strdup(pJSONValue);

         return true;
      }
   }

   return false;
}

bool ExtractString(const char *pSource, const char *pLeft, const char *pRight, char **pExtract)
{    
    size_t leftLen = strlen(pLeft);

    char *pLeftFound = strstr(pSource, pLeft);

    if (pLeftFound)
    {
        char *pRightRound = strstr(pLeftFound, pRight);

        if (pRightRound)
        {
            size_t extractLen = pRightRound - pLeftFound - leftLen;

            *pExtract = calloc(extractLen + 1, 1);

            strncpy(*pExtract, pLeftFound + leftLen, extractLen);

            return true;
        }
    }

    return false;
}

bool g_bLowDayArmed = false;
bool g_bDone = false;

void LogDigits(GHashTable *pKeyMap, const char *pDigits, int day)
{
    int digit = atoi(pDigits);

    char *pTrimmedDigit = NULL;

    asprintf(&pTrimmedDigit, "%d", digit);

    int *pCount = g_hash_table_lookup(pKeyMap, pTrimmedDigit);

    if (!pCount)
    {
        pCount = (int *) calloc(32, sizeof(int));                                                            

        g_hash_table_insert(pKeyMap, strdup(pTrimmedDigit), pCount);
    }

    if (day > 0 || day < 32)
    {
        pCount[day]++;
        pCount[0]++;
    }

    FreeString(&pTrimmedDigit);
}

json_t *g_pResponseArray;

void GetReport(const char *pURI, const char *pUserPass, char **pNextURI, GHashTable *pKeyMap)
{
    char *pURL;

    asprintf(&pURL, "https://api.twilio.com%s", pURI);

    char *pResponse = NULL;

    fprintf(stderr, "%s\n%s\n", pURL, pUserPass);

    int statusCode = GetHTTP(pURL, pUserPass, &pResponse);

    free(pURL);

    if (statusCode != 200)
    {
        return;
    }

    json_error_t err;

    json_t *pReport = json_loads(pResponse, 0, &err);

    if (pReport)
    {
        GetJSONString(pReport, pNextURI, "next_page_uri");

        json_t *pCallsArray = json_object_get(pReport, "calls");

        if (pCallsArray)
        {
            int arraySize = json_array_size(pCallsArray);

            for (int index=0; index<arraySize && !g_bDone; index++)
            {
                json_t *pCallJSON = json_array_get(pCallsArray, index);

                if (pCallJSON)
                {
                    char *pSID      = NULL; 
                    char *pTo       = NULL; 
                    char *pFrom     = NULL;
                    char *pStart    = NULL; 
                    char *pEnd      = NULL;
                    char *pDuration = NULL;

                    if (GetJSONString(pCallJSON, &pSID,      "sid") &&
                        GetJSONString(pCallJSON, &pFrom,     "from_formatted") &&
                        GetJSONString(pCallJSON, &pTo,       "to_formatted") &&
                        GetJSONString(pCallJSON, &pStart,    "start_time") &&
                        GetJSONString(pCallJSON, &pEnd,      "end_time") &&
                        GetJSONString(pCallJSON, &pDuration, "duration"))
                    {
                        if (strlen(pStart) > 26)
                        {                                                                                   ;
                            pStart[7] = 0;

                            int day = atoi(&pStart[5]);

                            char *pEventsURL;

                            asprintf(&pEventsURL, "https://api.twilio.com/2010-04-01/Accounts/AC5b4731b15db3d93a9f93b72ebeece5ea/Calls/%s/Events.json", pSID);

                            char *pEventsResponse = NULL;

                            int eventsStatusCode = GetHTTP(pEventsURL, "AC5b4731b15db3d93a9f93b72ebeece5ea:38b6e0a6c0332e4d54a6680bc944f78c", &pEventsResponse);

                            free(pEventsURL);

                            if (eventsStatusCode == 200)
                            {                    
                                json_error_t err;

                                json_t *pResponseJSON = json_loads(pEventsResponse, 0, &err);

                                if (pResponseJSON)
                                {
                                    json_array_append(g_pResponseArray, pResponseJSON);

                                    json_t *pEventsJSON = json_object_get(pResponseJSON, "events");

                                    if (pEventsJSON)
                                    {
                                        int arraySize = json_array_size(pEventsJSON);

                                        bool bFound = false;

                                        for (int index=0; index<arraySize; index++)
                                        {
                                            json_t *pEventJSON = json_array_get(pEventsJSON, index);

                                            if (pEventJSON)
                                            {
                                                json_t *pResponseJSON = json_object_get(pEventJSON, "response");

                                                if (pResponseJSON)
                                                {
                                                    char *pResponseBody;

                                                    if (GetJSONString(pResponseJSON, &pResponseBody, "response_body"))
                                                    {
                                                        char *pDigits = NULL;

                                                        if (ExtractString(pResponseBody, " number ", " will appear", &pDigits))
                                                        {
                                                            fprintf(stderr, "%s,%s,%s\n", &pStart[5], pDigits, pSID);

                                                            LogDigits(pKeyMap, pDigits, day);

                                                            free(pDigits);
                                                            free(pResponseBody);
                                                            bFound = true;

                                                            break;
                                                        }

                                                        free(pResponseBody);
                                                    }
                                                }
                                            }
                                        }

                                        if (!bFound)
                                        {   
                                            LogDigits(pKeyMap, "1000000000", day);
                                        }
                                    }

                                    json_decref(pResponseJSON);
                                }  
                                    
                                FreeString(&pEventsResponse);
                            }                    

                            //Log("%s,%s,%s,%s,%s,%s", pFrom, pTo, pStart, pEnd, pDuration, digits);
                        }

                        FreeString(&pSID);
                        FreeString(&pFrom);
                        FreeString(&pTo);        
                        FreeString(&pStart);
                        FreeString(&pEnd);
                        FreeString(&pDuration);                                                      
                    }
                }
            }
        }
    }

    FreeString(&pResponse);
}

static char doc[]      = "CAN-TV Utility";
static char args_doc[] = "";

static struct argp_option options[] = 
{
    {"startdate",  's', "2021-03-01",   0, "Start date"},
    {"enddate",    'e', "2021-03-31",   0, "End date"},
    {"account",    'a', "blank",        0, "Account ID"},  
    {"apikey",     'k', "blank",        0, "API Key"},   
    {"emailfrom",  'f', "me@gmail.com", 0, "From gmail account"},
    {"emailname",  'n', "My Name",      0, "Name i.e. John Smith"},
    {"emailto",    't', "you@gmail.com",0, "To e-mail account"},  
    {"emailpass",  'p', "mypassword",   0, "From gmail account password"},                 
    { 0 }
};

static error_t parse_opt (int key, char *arg, struct argp_state *state)
{
    CmdLineArgs *arguments = state->input;
    
    switch (key)
    {    
        case 's':
            arguments->pStartDate = arg;
            break;
        
        case 'f':
            arguments->pEmailFrom = arg;
            break;  
        
        case 'n':
            arguments->pEmailFromName = arg;
            break;                            

        case 't':
            arguments->pEmailTo = arg;
            break;  
        
        case 'p':
            arguments->pEmailPassword = arg;
            break;                                            

        case 'e':
            arguments->pEndDate = arg;
            break;

        case 'a':
            arguments->pAccount = arg;
            break;

        case 'k':    
            arguments->pAPIKey = arg;
            break;           

        case ARGP_KEY_ARG:         
            argp_usage(state);
            break;
        
        case ARGP_KEY_END:   
            break;
        
        default:         
            return ARGP_ERR_UNKNOWN;
    }
    
    return 0;
}

static struct argp argp = { options, parse_opt, args_doc, doc };

void ParseCommandLine(int argc, char **argv)
{
    g_cmdArgs.pAccount       = "AC5b4731b15db3d93a9f93b72ebeece5ea";
    g_cmdArgs.pAPIKey        = "38b6e0a6c0332e4d54a6680bc944f78c";
    g_cmdArgs.pStartDate     = "2021-03-01";
    g_cmdArgs.pEndDate       = "2021-03-31";
    g_cmdArgs.pEmailFrom     = "me@gmail.com";
    g_cmdArgs.pEmailTo       = "you@gmail.com";
    g_cmdArgs.pEmailFromName = "My Name";
    g_cmdArgs.pEmailPassword = "mypassword"; 

    argp_parse(&argp, argc, argv, 0, 0, &g_cmdArgs);    
}

void ShowStartup()
{
   fprintf(stderr, "Start Date : %s\n", g_cmdArgs.pStartDate);
   fprintf(stderr, "End Date   : %s\n", g_cmdArgs.pEndDate);
   fprintf(stderr, "Account    : %s\n", g_cmdArgs.pAccount);
   fprintf(stderr, "API Key    : %s\n", g_cmdArgs.pAPIKey);
   fprintf(stderr, "gmail From : %s\n", g_cmdArgs.pEmailFrom);
   fprintf(stderr, "gmail Name : %s\n", g_cmdArgs.pEmailFromName);
   fprintf(stderr, "gmail PW   : %s\n", g_cmdArgs.pEmailPassword);
   fprintf(stderr, "EMail To   : %s\n", g_cmdArgs.pEmailTo);   
}

typedef struct 
{
    GHashTable *pKeyValues;
    FILE       *pReportFile;
    int        totals[32];
} ShowContentsData;

void ShowContents(gpointer pKey, gpointer pUserData)
{
    ShowContentsData *pData =  (ShowContentsData *) pUserData;

    int *pValues = (int *) g_hash_table_lookup(pData->pKeyValues, pKey);

    if (pValues)
    {
        if (strcmp(pKey, "1000000000") == 0)
        {
            fprintf(pData->pReportFile, "Invalid");
        }
        else
        {
            fprintf(pData->pReportFile, "%s", (const char *) pKey);
        }

        for (int index=0; index<32; index++)
        {
            fprintf(pData->pReportFile, ",%d", pValues[index]);

            pData->totals[index] += pValues[index];                    
        }

        fprintf(pData->pReportFile,"\n");
    }
}

void Test()
{
    char *pTestURL;

    const char *pSID = "CA53c7354b6d2f15a2338d6165f4c83a9b";
    
    asprintf(&pTestURL, "https://api.twilio.com/2010-04-01/Accounts/AC5b4731b15db3d93a9f93b72ebeece5ea/Calls/%s/Events.json", pSID);

    char *pTestResponse = NULL;

    int response = GetHTTP(pTestURL, "AC5b4731b15db3d93a9f93b72ebeece5ea:38b6e0a6c0332e4d54a6680bc944f78c", &pTestResponse);

    free(pTestURL);

    if (response == 200)
    {
        fprintf(stderr, "%s\n", pTestResponse);
    }

    FreeString(&pTestResponse);    
}

int SortCallback(gconstpointer pA, gconstpointer pB)
{  
    const char *pCharA = *((char **) pA);
    const char *pCharB = *((char **) pB);

    int a = atoi(pCharA);
    int b = atoi(pCharB);

    if (a < b) return -1;
    if (a > b) return  1;

    return 0;
}

void AddKeyToArray(gpointer pKey, gpointer pValue, gpointer pUserData)
{
    GPtrArray *pKeyArray = (GPtrArray *) pUserData;

    g_ptr_array_insert(pKeyArray, -1, pKey);
}

#define REPORT_HEADER "Keys,Total,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31\n"

void main(int argc, char **pArgv)
{
    ParseCommandLine(argc, pArgv);

    ShowStartup();

#ifdef COMMENT_OUT

    char *pStartYMDHMS, *pEndYMDHMS, *pURL, *pUserPass;

    asprintf(&pStartYMDHMS, "%sT00:00:00-00:00", g_cmdArgs.pStartDate);
    asprintf(&pEndYMDHMS,   "%sT23:59:59-00:00", g_cmdArgs.pEndDate);
    asprintf(&pUserPass,    "%s:%s",             g_cmdArgs.pAccount, g_cmdArgs.pAPIKey);

    asprintf(&pURL, "/2010-04-01/Accounts/%s/Calls.json?StartTime>=%s&EndTime<=%s", g_cmdArgs.pAccount, pStartYMDHMS, pEndYMDHMS);

    free(pStartYMDHMS);
    free(pEndYMDHMS);

    GHashTable *pKeyMap = g_hash_table_new_full(g_str_hash, g_str_equal, free, free);

    json_t *pDump = json_object();

    g_pResponseArray = json_array();    

    json_object_set_new(pDump, "responses", g_pResponseArray);    

    char *pNextURL = NULL;    
    
    while (1)
    {
        pNextURL = NULL;

        GetReport(pURL, pUserPass, &pNextURL, pKeyMap);
        
        if (!pNextURL)
        {
            break;
        }
        
        free(pURL);
        pURL = pNextURL;        
    }

    free(pURL);
    free(pUserPass);

    json_dump_file(pDump, "dump.json", 0);

    json_decref(pDump);

    ShowContentsData data;

    for (int index=0; index<32; index++)
    {
        data.totals[index] = 0;
    }    

    data.pReportFile = fopen("report.csv", "wb");    

    if (data.pReportFile)
    {
        GPtrArray *pKeyArray = g_ptr_array_new();

        g_hash_table_foreach(pKeyMap, AddKeyToArray, pKeyArray);

        g_ptr_array_sort(pKeyArray, SortCallback); 

        data.pKeyValues = pKeyMap;

        fprintf(data.pReportFile, REPORT_HEADER);

        g_ptr_array_foreach(pKeyArray, ShowContents, &data);  

        fprintf(data.pReportFile, "Total");

        for (int index=0; index<32; index++)
        {
            fprintf(data.pReportFile, ",%d", data.totals[index]);
        }

        g_ptr_array_free(pKeyArray, false);

        fclose(data.pReportFile);    
#endif
        struct stat fileStat;

        if (stat("report.csv", &fileStat) == 0)
        {
            FILE *pCSVFile = fopen("report.csv", "rb");

            if (pCSVFile)
            {   
                unsigned char *pFileData = calloc(fileStat.st_size + 1, 1);

                fread(pFileData, 1, fileStat.st_size, pCSVFile);

                fclose(pCSVFile);

                size_t encodedSize;

                char *pEncodedData = base64_encode(pFileData, fileStat.st_size, &encodedSize);

                SendEmail("report.csv", pEncodedData);

                free(pEncodedData);

                free(pFileData);
            }    
        }
#ifdef COMMENT_OUT        
    }

    g_hash_table_destroy(pKeyMap);    
#endif    
}