#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cjson/cJSON.h>

#define MAX_BUFFER_SIZE 20000
#define MAX_SOURCES 100
#define PORT_PUBLISHER 8081

// Function to read the contents of a file into a buffer
size_t read_file_to_buffer(const char *filename, char *buffer)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        perror("Error opening file");
        exit(1);
    }

    // Move the file pointer to the end to get the file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    rewind(file); // Move the file pointer back to the beginning

    // Check if the buffer is large enough
    if (file_size >= MAX_BUFFER_SIZE)
    {
        fprintf(stderr, "File is too large for the buffer\n");
        exit(1);
    }

    // Read the file content into the buffer
    size_t bytes_read = fread(buffer, 1, file_size, file);
    buffer[bytes_read] = '\0'; // Null-terminate the buffer for safe string operations

    // Close the file
    fclose(file);

    return bytes_read;
}

// Function to send a single article to the broker
void publish_article(int sockfd, cJSON *article)
{
    // Convert the JSON article object to a string
    char *json_str = cJSON_Print(article);

    // Send the article to the broker
    if (send(sockfd, json_str, strlen(json_str), 0) == -1)
    {
        perror("Failed to send article");
    }
    else
    {
        printf("Published Article: %s\n", json_str);
    }

    sleep(1);

    // Cleanup
    free(json_str);
}

// Function to publish articles one by one
void publish_articles(int sockfd, cJSON *articles)
{
    // Filter and publish articles based on source name
    int article_count = cJSON_GetArraySize(articles);
    for (int i = 0; i < article_count; i++)
    {
        cJSON *article = cJSON_GetArrayItem(articles, i);
        if (article != NULL)
        {
            // Get the source of the article
            cJSON *source = cJSON_GetObjectItem(article, "source");
            if (source != NULL)
            {
                cJSON *name = cJSON_GetObjectItem(source, "name");
                if (name != NULL)
                {
                    // Check if the source name matches "BBC", "CNN", or "Reuters"
                    if (strcmp(name->valuestring, "BBC") == 0 ||
                        strcmp(name->valuestring, "CNN") == 0 ||
                        strcmp(name->valuestring, "Reuters") == 0)
                    {
                        // Publish only if the source matches
                        publish_article(sockfd, article);
                    }
                }
            }
        }
    }
}

// Function to process the JSON and filter sources
// void process_news_json(const char *json_data, char *sources[], int max_sources)
void process_news_json(const char *json_data)
{
    cJSON *root = cJSON_Parse(json_data);
    if (root == NULL)
    {
        fprintf(stderr, "Error parsing JSON\n");
        return;
    }

    cJSON *articles = cJSON_GetObjectItem(root, "articles");
    if (articles == NULL)
    {
        fprintf(stderr, "No articles found in JSON\n");
        cJSON_Delete(root);
        return;
    }

    int sockfd;
    struct sockaddr_in broker_addr;

    // Create socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Socket creation failed");
        return;
    }

    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons(PORT_PUBLISHER);
    broker_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    // Connect to the broker
    if (connect(sockfd, (struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0)
    {
        perror("Connection to broker failed");
        return;
    }

    // Publish articles to the broker one by one
    publish_articles(sockfd, articles);

    // Cleanup
    cJSON_Delete(root);

    // Close the socket
    close(sockfd);
}

int main()
{
    char buffer[MAX_BUFFER_SIZE] = {0}; // Buffer to store the JSON data

    const char *filename = "news_articles.json"; // Path to your JSON file

    // Read the file into the buffer
    size_t bytes_read = read_file_to_buffer(filename, buffer);
    if (bytes_read == 0)
    {
        perror("Couldn't load data");
        exit(1);
    }

    // Process the fetched JSON data and filter by source
    process_news_json(buffer);
    return 0;
}
