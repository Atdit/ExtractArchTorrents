#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>

/* Fix unresponsive CLion when using libcurl */
#ifdef __JETBRAINS_IDE__
#define CURL_DISABLE_TYPECHECK
#endif

#include <curl/curl.h>

#define COMMAND "transmission-remote -a %s" // %s is the extracted torrent link
#define TORRENT_DOWNLOAD_PATH "<path>" // Path to the directory where torrents are downloaded
#define WEBSITE "https://archlinux.org/download/"

typedef struct {
    char *memory;
    size_t size;
    size_t allocated;
} curl_callback_t;

unsigned long nanos(int mode) {
    struct timespec spec;
    if (clock_gettime(mode, &spec) == -1) return 0;
    return spec.tv_sec * 1000000000L + spec.tv_nsec;
}

size_t curl_callback_f(void *content, size_t size, size_t nmemb, void *data) {
    curl_callback_t *callback = (curl_callback_t *) data;

    size_t total_size = size * nmemb;

    char *new_memory = callback->memory;
    if (callback->allocated < callback->size + total_size + 1) {
        size_t new_memory_size = (size_t) ((double) (callback->size + total_size + 1) * 1.5); // Grow by 1.5x
        new_memory = realloc(callback->memory, new_memory_size); // + 1 for the '\0' terminator
        if (new_memory == NULL) {
            fprintf(stderr, "Fatal Error: realloc() returned NULL\n");
            exit(EXIT_FAILURE);
        }
        callback->allocated = new_memory_size;
    }

    callback->memory = new_memory;
    memcpy(callback->memory + callback->size, content, total_size);
    callback->size += total_size;
    callback->memory[callback->size] = '\0';

    return total_size;
}

int does_exist(char *date) {
    DIR *dir;
    struct dirent *dirent;
    dir = opendir(TORRENT_DOWNLOAD_PATH);
    if (dir == NULL) {
        fprintf(stderr, "Error %d (%s) in opendir()\n", errno, strerror(errno));
        return 0;
    }

    char target_substr[128];
    snprintf(target_substr, sizeof target_substr, "archlinux-%s-x86_64.iso", date);
    int exists = 0;
    while ((dirent = readdir(dir)) != NULL) {
        char *name = dirent->d_name;
        if (strcmp(name, ".") == 0) continue;
        if (strcmp(name, "..") == 0) continue;
        if (strstr(name, target_substr) != NULL) {
            exists = 1;
            break;
        }
    }
    closedir(dir);

    printf("Torrent with date <%s> %s\n", date, exists ? "found" : "not found");

    return exists;
}

int main() {
    printf("Running at system time %ld\n", nanos(CLOCK_REALTIME));

    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        fprintf(stderr, "curl_easy_init() failed\n");
        return EXIT_FAILURE;
    }

    curl_callback_t callback = {0};

    curl_easy_setopt(curl, CURLOPT_URL, WEBSITE);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_callback_f);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *) &callback);

    CURLcode response = curl_easy_perform(curl);
    if (response != CURLE_OK) {
        fprintf(stderr, "curl_easy_perfrom() failed: %s\n", curl_easy_strerror(response));
        return EXIT_FAILURE;
    }

    char torrent_link[256] = {0}, date_str[128] = {0};
    strcpy(torrent_link, "https://archlinux.org");
    char *current = callback.memory;
    for (; *current != '\0'; current++) {
        if (*current == 'h' && strncmp(current, "href=\"/releng/releases", 22) == 0) {
            strncat(torrent_link, current + 6, 38);
            strncpy(date_str, current + 23, 10);
            date_str[10] = '\0';
            torrent_link[57] = 0; // Final link is 57 characters long

            printf("%s\n", torrent_link);

            if (!does_exist(date_str)) {
                char command[256];
                snprintf(command, sizeof command, COMMAND "", torrent_link);
                system(command);
            }
        }
    }

    curl_easy_cleanup(curl);

    return EXIT_SUCCESS;
}
