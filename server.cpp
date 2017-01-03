#include <vector>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <cerrno>
#include <string>
#include <pthread.h>
#include <semaphore.h>
#include <queue>
#include <sstream>

#define SOCKET_ERROR        -1
#define BUFFER_SIZE         100
#define MESSAGE             "This is the message I'm sending back and forth"
#define QUEUE_SIZE          5
#define MAX_MSG_SZ      1024
#define THREADS 			10

using namespace std;

int hSocket,hServerSocket;  /* handle to socket */
struct hostent* pHostInfo;   /* holds info about a machine */
struct sockaddr_in Address; /* Internet socket address struct */
int nAddressSize=sizeof(struct sockaddr_in);
char pBuffer[BUFFER_SIZE];
int nHostPort;
int numThreads;
string directory;
string filepath;
string response;
string filetype;

int fd;
vector<char *> headerLines;
char buffer[MAX_MSG_SZ];
char contentType[MAX_MSG_SZ];

queue<int> tasks;
sem_t work_mutex;
sem_t space_on_q;
sem_t n_of_tasks;

struct thread_info
{
	int thread_id;
	int another_number;
};
	
// Determine if the character is whitespace
bool isWhitespace(char c)
{ switch (c)
    {
        case '\r':
        case '\n':
        case ' ':
        case '\0':
            return true;
        default:
            return false;
    }
}

// Strip off whitespace characters from the end of the line
void chomp(char *line)
{
    int len = strlen(line);
    while (isWhitespace(line[len]))
    {
        line[len--] = '\0';
    }
}

// Read the line one character at a time, looking for the CR
// You dont want to read too far, or you will mess up the content
char * GetLine(int fds)
{
    char tline[MAX_MSG_SZ];
    char *line;
    
    int messagesize = 0;
    int amtread = 0;
    while((amtread = read(fds, tline + messagesize, 1)) < MAX_MSG_SZ)
    {
        if (amtread > 0)
            messagesize += amtread;
        else
        {
            perror("Socket Error is:");
            fprintf(stderr, "Read Failed on file descriptor %d messagesize = %d\n", fds, messagesize);
            //exit(2);
        }
        //fprintf(stderr,"%d[%c]", messagesize,message[messagesize-1]);
        if (tline[messagesize - 1] == '\n')
            break;
    }
    tline[messagesize] = '\0';
    chomp(tline);
    line = (char *)malloc((strlen(tline) + 1) * sizeof(char));
    strcpy(line, tline);
    //fprintf(stderr, "GetLine: [%s]\n", line);
    return line;
}
    
// Change to upper case and replace with underlines for CGI scripts
void UpcaseAndReplaceDashWithUnderline(char *str)
{
    int i;
    char *s;
    
    s = str;
    for (i = 0; s[i] != ':'; i++)
    {
        if (s[i] >= 'a' && s[i] <= 'z')
            s[i] = 'A' + (s[i] - 'a');
        
        if (s[i] == '-')
            s[i] = '_';
    }
    
}


// When calling CGI scripts, you will have to convert header strings
// before inserting them into the environment.  This routine does most
// of the conversion
char *FormatHeader(char *str , string pre)
{
	char *prefix;
	strcpy(prefix, pre.c_str());
    char *result = (char *)malloc(strlen(str) + strlen(prefix));
    char* value = strchr(str,':') + 2;
    UpcaseAndReplaceDashWithUnderline(str);
    *(strchr(str,':')) = '\0';
    sprintf(result, "%s%s=%s", prefix, str, value);
    return result;
}

// Get the header lines from a socket
//   envformat = true when getting a request from a web client
//   envformat = false when getting lines from a CGI program

void GetHeaderLines(vector<char *> &headerLines, int skt, bool envformat)
{
    // Read the headers, look for specific ones that may change our responseCode
    char *line;
    char *tline;
    
    tline = GetLine(skt);
    while(strlen(tline) != 0)
    {
        if (strstr(tline, "Content-Length") || 
            strstr(tline, "Content-Type"))
        {
            if (envformat)
            {
                line = FormatHeader(tline, "");
            }
            else
                line = strdup(tline);
        }
        else
        {
            if (envformat)
            {
                line = FormatHeader(tline, "HTTP_");
            }
            else
            {
                line = (char *)malloc((strlen(tline) + 10) * sizeof(char));
                sprintf(line, "%s", tline);                
            }
        }
        //fprintf(stderr, "Header --> [%s]\n", line);
        
        headerLines.push_back(line);
        free(tline);
        tline = GetLine(skt);
    }
    free(tline);
}


//read file in
string get_file_contents(const char *filename)
{
	ifstream in(filename, ios::in | ios::binary);
	if(in)
	{
		string contents;
		in.seekg(0, ios::end);
		contents.resize(in.tellg());
		in.seekg(0, ios::beg);
		in.read(&contents[0], contents.size());
		in.close();
		return(contents);
	}
	else
	{
		perror("ERROR: could not read all the file contents from disk");
		return "";
	}
}

string createLink(string folder, string filename) {

	string link = "";
	link.append("\n<a href=\"");
	if(folder.length() == 0) {
		folder.append("/");
	}
	if(folder[folder.length() - 1] != '/') {
		folder.append("/");
	}
	link.append(folder.append(filename));
	link.append("\"> ");
	link.append(filename);
	link.append("</a><br>");
	
	return link;
}

string createListing(string folder, string filepath) {
	
	string links;
	string files;
	int bufferSize = 1024;
	char buffer[bufferSize];
	FILE* stream;
	int rval;
	string commands;
	commands.append("cd ");
	commands.append(filepath);
	commands.append("; ls");
	//get filenames out
	stream = popen (commands.c_str(), "r");
	while((rval = fread(buffer, 1, bufferSize, stream)) > 0) {
		printf("fread returned %d\n",rval);
		files = files.append(buffer);
		links.append("<!DOCTYPE html>\n<html>\n<body>\n");
		//create link for each file
		while(files.find("\n") != string::npos) {
			string file = files.substr(0, files.find("\n"));
			files = files.substr(files.find("\n") + 1);
			links.append(createLink(folder,file));
		}
		links.append("\n\n</body>\n</html>");
	}
	pclose (stream);
	
	//parse filenames
	
	return links;
}

string createResponse(string file, string filetype) {
	
	if(file != "404") {
		string response;
		stringstream length;
		length << file.length(); 
		// add headers
		response.append("HTTP/1.1 200 OK\r\n");
		response.append("Content-Length: ");
		response.append(length.str());
		response.append("\r\nContent-Type: ");
		if(filetype == "html")
		{
			response.append("text/html");
		}
		else if(filetype == "txt")
		{
			response.append("text/plain");
		}
		else if(filetype == "jpg" || filetype == "gif")
		{
			response.append("image/" + filetype);
		}
		else 
		{
			response.append("text/html");
		}
		response.append("\r\n\r\n");
		
		// add message
		response.append(file);
		
		return response;
	}
	//if error, send back appropriate response
	else {	
		cout << "404!" << endl;
		string error = "<DOCTYPE html>\n<html>\n<body>Sorry the File could not be found\n</body>\n</html>";
		stringstream length;
		length << error.length(); 
		response.append("HTTP/1.1 404 Not Found\r\nContent-Length: ");
		response.append(length.str());
		response.append("\r\nContent-Type: text/html\r\n\r\n");
		response.append(error);
		
		return response;
	}	

}

void* serve( void* in_data )
{
    struct thread_info* t_info = ( struct thread_info* ) in_data;
    int tid = t_info->thread_id;

    while( 1 )
    {
    	//set semaphores
    	sem_wait(&n_of_tasks);
        sem_wait( &work_mutex );
        
        int hSocket = tasks.front();
        tasks.pop();
		
		//release work_mutex so queue can be accessed
		sem_post( &work_mutex );

		
        //cout << "thread | socket: " << tid << "\t";
        //cout << hSocket << endl;
        
        printf("\nGot a connection\n");
        
         //make port re-usable quickly
		int optval = 1;
		setsockopt (hSocket, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
        
		// Read the header lines
		headerLines.clear();
		GetHeaderLines(headerLines, hSocket , false);

		  
		  // extract filepath
		  filepath = "";
		  filetype = "";
		  
		  for (int i = 0; i < headerLines.size(); i++) {
			if(strstr(headerLines[i], "GET")) {
				//extract the filepath and filetype
				string remainder(headerLines[i]);
				remainder = remainder.substr(4);
				filepath = remainder.substr(0, remainder.find(" "));
				filetype = filepath.substr(filepath.find(".") + 1);
				//cout << "\nfiletype:\n" << filetype << endl;
				//cout << "\nfilepath:\n" << filepath << "\n\n";
			}
		  }
		  
		// use stat to find the filesize and type (dir/file)
	  	struct stat filestat;
		string fullpath(directory);
		fullpath.append(filepath);
		string file = "";
		bool isFile = false;
		bool isDirectory = false;
		
		cout << "Searching for: " << fullpath << endl;
		if(stat(fullpath.c_str(), &filestat) == -1) {
			cout <<"ERROR in stat\n";
			perror("stat");
		}
		if(S_ISREG(filestat.st_mode)) {
			isFile = true;
			//cout << filepath << " is a regular file \n";
			//cout << "file size = "<<filestat.st_size <<"\n";
		}
		if(S_ISDIR(filestat.st_mode)) {
			isDirectory = true;
			//cout << filepath << " is a directory \n";
		}
  		
  		//load index or create list if directory
  		if(isDirectory)
  		{
  			string indexpath = fullpath;
  			indexpath.append("/index.html");
  			
			if(stat(indexpath.c_str(), &filestat) == -1) {
				//cout <<"No index.html file\n";
				//create an html list of links to files within
				file = createListing(filepath, fullpath);
			}
			else if(S_ISREG(filestat.st_mode)) {
				//cout << "index size = "<< filestat.st_size <<"\n";
				
				// load file
  				file = get_file_contents(indexpath.c_str());
			}
  		}
  		// load file if file
  		else if(isFile)
  		{
	  		// load file
	  		file = get_file_contents(fullpath.c_str());
	  		cout << "File Loaded\n" << file << endl;
	  	}
	  	else {
	  		//error, send back 404
	  		cout << "File = 404" << endl;
	  		file = "404";
	  	}
  	
  	
		// create response
		response.clear();
		response = createResponse(file, filetype);
		
		//send response back to the client
  		//printf("\nSending \"%s\" to client\n", response.c_str());
  		printf("Sending doc to client\n");
  		
  		int responseLength = response.length();
  		int amountWritten = 0;
  		while(amountWritten < responseLength)
  		{
  			write(hSocket, response.substr(amountWritten, amountWritten + BUFFER_SIZE).c_str(), BUFFER_SIZE);
  			amountWritten += BUFFER_SIZE;
  		}


    	printf("\nClosing the socket");

        /* close socket */
        shutdown(hSocket, SHUT_RDWR);
        if(close(hSocket) == SOCKET_ERROR)
        {
         printf("\nCould not close socket\n");
         return 0;
        }

        
        sem_post(&space_on_q);
        
        // do work with FD/Socket
    }
}

int main(int argc, char* argv[])
{

	//------Get inputs
    if(argc < 4)
      {
        printf("\nUsage: server <port number> <number threads> <working directory>\n");
        return 0;
      }
    else
      {
        nHostPort=atoi(argv[1]);
        numThreads = atoi(argv[2]);
        directory = argv[3];
      }
      
      
    //------------Create Thread Pool------------------
    sem_init( &work_mutex, 0, 1 );
	sem_init(&space_on_q, 0, 100);
	sem_init(&n_of_tasks, 0, 0);
	
    pthread_t threads[ numThreads ];

    struct thread_info all_thread_info[ numThreads ];

    for( int i = 0; i < numThreads; i++ )
    {
        sem_wait( &work_mutex );
 
        cout << "creating thread: " << i << "\t" << endl;
        all_thread_info[ i ].thread_id = i;
        pthread_create( &threads[ i ], NULL, serve, ( void* ) &all_thread_info[ i ] );
 
        sem_post( &work_mutex );
    }
	//--------------------------------------------------
	
    printf("\nStarting server");
    printf("\nMaking socket");
    /* make a socket */
    hServerSocket=socket(AF_INET,SOCK_STREAM,0);

    if(hServerSocket == SOCKET_ERROR)
    {
        printf("\nCould not make a socket\n");
        return 0;
    }

    /* fill address struct */
    Address.sin_addr.s_addr=INADDR_ANY;
    Address.sin_port=htons(nHostPort);
    Address.sin_family=AF_INET;

    printf("\nBinding to port %d\n",nHostPort);

    /* bind to a port */
    if(bind(hServerSocket,(struct sockaddr*)&Address,sizeof(Address)) 
                        == SOCKET_ERROR)
    {
        printf("\nCould not connect to host\n");
        return 0;
    }
    
 	/*  get port number */
    getsockname( hServerSocket, (struct sockaddr *) &Address,(socklen_t *)&nAddressSize);
    printf("opened socket as fd (%d) on port (%d) for stream i/o\n",hServerSocket, ntohs(Address.sin_port) );

        printf("Server\n\
              sin_family        = %d\n\
              sin_addr.s_addr   = %d\n\
              sin_port          = %d\n"
              , Address.sin_family
              , Address.sin_addr.s_addr
              , ntohs(Address.sin_port)
            );


    printf("\nMaking a listen queue of %d elements",QUEUE_SIZE);
    /* establish listen queue */
    if(listen(hServerSocket,QUEUE_SIZE) == SOCKET_ERROR)
    {
        printf("\nCould not listen\n");
        return 0;
    }
	
	//----------------Have threads accept new requests forever---------------
    while( 1 )
    {
        
        printf("\nWaiting for a connection\n");

        /* receive a request */
        hSocket=accept(hServerSocket,(struct sockaddr*)&Address,(socklen_t *)&nAddressSize);
        
		if(hSocket == -1) {
			cout << "error, could not accept socket" << endl;
		}
        else {
			/* add request to queue */
			sem_wait(&space_on_q);	//wait til there is room on queue to add 
			sem_wait(&work_mutex);
		
			tasks.push(hSocket);
		
			sem_post(&work_mutex);
			sem_post(&n_of_tasks);		//increment n_of_tasks one so a thread can pick it up
		}
    }


	if(close(hServerSocket) == SOCKET_ERROR) {
		 printf("\nCould not close main socket\n");
		 return 0;
	}
}
