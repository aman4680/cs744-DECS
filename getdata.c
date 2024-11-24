#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <unistd.h>

#define NEWS_API_KEY "b8e7c1b9de59444dab8d104237e2e098"
#define NEWS_API_URL "https://newsapi.org/v2/top-headlines?country=us&apiKey=" NEWS_API_KEY

// Write callback for curl - This is where the data will be written to a file
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *data)
{
    size_t total_size = size * nmemb;
    FILE *file = (FILE *)data; // File pointer passed from main function

    // Write the data to the file
    size_t written = fwrite(ptr, 1, total_size, file);
    return written; // Return the number of bytes written
}

// Function to fetch news articles using NewsAPI and save it to a file
void fetch_news_articles_and_save_to_file(const char *filename)
{
    CURL *curl;
    CURLcode res;

    // Open the file where the data will be saved
    FILE *file = fopen(filename, "w");
    if (!file)
    {
        perror("Failed to open file for writing");
        return;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();

    if (curl)
    {
        // Set the URL for the API request
        printf("Setting URL: %s\n", NEWS_API_URL);
        curl_easy_setopt(curl, CURLOPT_URL, NEWS_API_URL);

        // Set the write callback function
        printf("Setting the write callback function...\n");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);

        // Pass the file pointer to the write callback function
        printf("Setting the file pointer...\n");
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        // Set the User-Agent header to avoid the "userAgentMissing" error
        printf("Setting the User-Agent header...\n");
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "MyNewsAggregator/1.0 (http://example.com)");

        // Enable verbose mode for debugging (optional)
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

        // Perform the request
        printf("Performing the request...\n");
        res = curl_easy_perform(curl);

        // Check if there was an error during the request
        if (res != CURLE_OK)
        {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        }
        else
        {
            // Success, print the HTTP response code
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            printf("HTTP response code: %ld\n", http_code);
        }

        // Clean up the curl object
        curl_easy_cleanup(curl);
    }
    else
    {
        fprintf(stderr, "Error initializing curl\n");
    }

    // Clean up global curl settings
    curl_global_cleanup();

    // Close the file
    fclose(file);
    printf("Data saved to %s\n", filename);
}

int main()
{
    // Fetch news articles and save to a file
    fetch_news_articles_and_save_to_file("news_articles.json");

    return 0;
}
