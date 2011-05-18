#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <curl/curl.h>

#define MAX_URL_LENGTH 1024
#define DEFAULT_SERVER "http://mediabox:9090/add_player/"

int SendCharacterInit()
{
    /* In windows, this will init the winsock stuff */ 
    curl_global_init(CURL_GLOBAL_ALL);
    
    return 0;
}

int SendCharacterCleanup()
{
    curl_global_cleanup();
    
    return 0;
}

int SendCharacter(const char *file, const char *playername)
{
    CURL *curl;
    CURLcode res;
    FILE *hd_src;
    int hd;
    struct stat file_info;
    
    char url[MAX_URL_LENGTH];
    strcpy(url, DEFAULT_SERVER);

    /* get the file size of the local file */ 
    hd = open(file, O_RDONLY);
    if (!hd) {
        printf("open %s failed %d\n",file,errno);
        return -1;
    }
    fstat(hd, &file_info);
    close(hd);

    /* get a FILE * of the same file, could also be made with
     fdopen() from the previous descriptor, but hey this is just
     an example! */ 
    hd_src = fopen(file, "rb");
    if (!hd_src) {
        printf("fopen %s failed %d\n",file,errno);
        return -1;
    }

    /* get a curl handle */ 
    curl = curl_easy_init();
    if(curl) {
        char *cleanplayer = curl_easy_escape(curl, playername, 0);
        strncat(url, cleanplayer, MAX_URL_LENGTH-100);
        curl_free(cleanplayer);
    
        /* enable uploading */ 
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

        /* HTTP PUT please */ 
        curl_easy_setopt(curl, CURLOPT_PUT, 1L);

        /* specify target URL, and note that this URL should include a file
           name, not only a directory */ 
        curl_easy_setopt(curl, CURLOPT_URL, url);

        /* now specify which file to upload */ 
        curl_easy_setopt(curl, CURLOPT_READDATA, hd_src);

        /* provide the size of the upload, we specicially typecast the value
           to curl_off_t since we must be sure to use the correct data size */ 
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                         (curl_off_t)file_info.st_size);

        /* Now run off and do what you've been told! */ 
        res = curl_easy_perform(curl);
        if (res) printf("curl failed %s\n",url);

        /* always cleanup */ 
        curl_easy_cleanup(curl);
    }
    fclose(hd_src); /* close the local file */ 
    
    return 0;
}

