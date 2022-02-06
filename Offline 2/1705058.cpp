#include<iostream>
#include<fstream>
#include<string>
#include<random>
#include<pthread.h>
#include<semaphore.h>
#include<unistd.h>
#include<chrono>

#define TOTAL_ARRIVALS 10
#define SIMULATION_TIME_MINUTES 60.0
#define PRINT_TO_CONSOLE true
#define PRINT_TO_FILE true
using namespace std;
using namespace chrono;

/*-------------------------Passenger Structure-------------------------*/
struct Passenger
{
    int PassengerID = -1;
    int ArrivalTime = -1;
    int VIP = 0; // 1 for VIP
    int KioskNumber = -1;
    int SecurityBelt = -1;
    int HasBoardingPass = -1;
    int BoardingComplete = 0;
    string Identity;

    void CopyPassenger(Passenger pass){
        PassengerID = pass.PassengerID;
        ArrivalTime = pass.ArrivalTime;
        VIP = pass.VIP;
        KioskNumber = pass.KioskNumber;
        SecurityBelt = pass.SecurityBelt;
        HasBoardingPass = pass.HasBoardingPass;
        Identity = pass.Identity;
    }
};


/*-------------------------Global Variables-------------------------*/
// Program
int* Kiosk;
Passenger AllPassenger[TOTAL_ARRIVALS];
pthread_t AllThreads[TOTAL_ARRIVALS];
int M, N, P, W, X, Y, Z; // Given values from file
pthread_mutex_t print_mutex;    // Self explanatory
time_point<steady_clock> StartTime;
int FirstPassengerTime = 0;
ofstream OutputFile;

// Kiosk
pthread_mutex_t kiosk_check_mutex;  // Used when checking available kiosk
pthread_mutex_t* kiosk_mutex;   // Used when passenger goes inside a kiosk
sem_t kiosk_sem;    // Keeps count of available kiosk

// Security Belt
sem_t* security_belt_sem; // Keeps count of available space in each security belt

// Refined VIP
int LTRCount = 0; // Keeps count of passenger going from left to right
int RTLCount = 0; // Keeps count of passenger going from right to left
pthread_mutex_t ltr_count_mutex; // Mutex for accessing LTRCount
pthread_mutex_t rtl_count_mutex; // Mutex for accessing RTLCount
pthread_mutex_t vip_way_mutex; // Mutex used for prioritizing Left to Right Direction
pthread_mutex_t channel_mutex; // Mutex for locking the channel

// Boarding
pthread_mutex_t boarding_check_mutex; // Mutex for locking the boarding area which has capacity of 1

// Special Kiosk
pthread_mutex_t special_kiosk_mutex; // Mutex for locking the special kiosk which has capacity of 1

/*-------------------------Utilities-------------------------*/

// Prioritizing empty kiosk, this function returns an empty kiosk
int GetEmptyKiosk(){
    for(int i=0; i<M; i++){
        if(Kiosk[i] == 1){
            return i;
        }
    }
    return -1;
}

// A function to write output to console
void PrintWithTime(string ToPrint){
    pthread_mutex_lock(&print_mutex);

    duration<double> Diff = steady_clock::now() - StartTime;
    int Time = (int) Diff.count() + FirstPassengerTime;
    if(PRINT_TO_CONSOLE){
        cout << ToPrint << " at time " << Time << endl;
    }
    if(PRINT_TO_FILE){
        OutputFile << ToPrint << " at time " << Time << endl;
    }
    pthread_mutex_unlock(&print_mutex);
}

// A function which simulates the self check in kiosk
void SelfCheckUp(Passenger* passenger){
    // Find an empty kiosk
    pthread_mutex_lock(&kiosk_check_mutex);
    sem_wait(&kiosk_sem);
    int NextKiosk = GetEmptyKiosk();
    Kiosk[NextKiosk] = 0;
    passenger->KioskNumber = NextKiosk;
    pthread_mutex_unlock(&kiosk_check_mutex);

    // Lock the kiosk and do self checkup
    pthread_mutex_lock(&kiosk_mutex[passenger->KioskNumber]);
    PrintWithTime("Passenger " + passenger->Identity + " has started self-check in kiosk " + to_string((passenger->KioskNumber+1)));

    sleep(W);
    
    PrintWithTime("Passenger " + passenger->Identity + " has finished self-check");

    Kiosk[passenger->KioskNumber] = 1;
    pthread_mutex_unlock(&kiosk_mutex[passenger->KioskNumber]);
    sem_post(&kiosk_sem);
}

// A function which simulates the Security Check for non VIP
void SecurityBeltnonVIP(Passenger* passenger){
    // Join a security belt
    int SecurityBelt = rand()%N;
    passenger->SecurityBelt = SecurityBelt;

    PrintWithTime("Passenger " + passenger->Identity + " has started waiting for security check in belt " + to_string(passenger->SecurityBelt+1));

    // Do checkup, wait if the belt is not empty
    sem_wait(&security_belt_sem[passenger->SecurityBelt]);

    PrintWithTime("Passenger " + passenger->Identity + " has started the security check in belt " + to_string(passenger->SecurityBelt+1));
    sleep(X);
    PrintWithTime("Passenger " + passenger->Identity + " has crossed security check");

    sem_post(&security_belt_sem[passenger->SecurityBelt]);
}

// A function which simulates the VIP Channel going forward
void LeftToRight(Passenger* passenger){
    PrintWithTime("Passenger " + passenger->Identity + " has arrived in front of VIP Channel");

    // Left to right count increase
    pthread_mutex_lock(&ltr_count_mutex);
    LTRCount++;

    if(LTRCount==1){
        pthread_mutex_lock(&vip_way_mutex); // Locking this mutex blocks new passenger from entering the VIP Channel from right to left
        pthread_mutex_lock(&channel_mutex); // Locks the channel
    }
    pthread_mutex_unlock(&ltr_count_mutex);

    // Pass the VIP Channel
    PrintWithTime("Passenger " + passenger->Identity + " has started passing through VIP Channel");
    sleep(Z);
    PrintWithTime("Passenger " + passenger->Identity + " has crossed VIP Channel");

    // Left to right count decrease
    pthread_mutex_lock(&ltr_count_mutex);
    LTRCount--;

    if(LTRCount==0){
        pthread_mutex_unlock(&vip_way_mutex); // Unlocking this mutex allows passenger going backwards board the channel
        pthread_mutex_unlock(&channel_mutex); // The channel is available again
    }

    pthread_mutex_unlock(&ltr_count_mutex);
}

// A function which simulates the VIP Channel going backward
void RightToLeft(Passenger* passenger){
    PrintWithTime("Passenger " + passenger->Identity + " has arrived in front of VIP Channel to go backward");

    pthread_mutex_lock(&vip_way_mutex); // As long as there are passenger going from left to right, this will never be unlocked to begin with, so, passenger will wait
    pthread_mutex_unlock(&vip_way_mutex); // It is unlocked immedietly so that other passenger waiting may also get on the channel

    // Increase right to left count
    pthread_mutex_lock(&rtl_count_mutex);
    RTLCount++;

    if(RTLCount==1){
        pthread_mutex_lock(&channel_mutex); // Lock the channel
    }
    pthread_mutex_unlock(&rtl_count_mutex);

    // Pass the channel
    PrintWithTime("Passenger " + passenger->Identity + " has started passing through VIP Channel backward");
    sleep(Z);
    PrintWithTime("Passenger " + passenger->Identity + " has crossed VIP Channel backward");

    // Decrease right to left count
    pthread_mutex_lock(&rtl_count_mutex);
    RTLCount--;

    if(RTLCount==0){
        pthread_mutex_unlock(&channel_mutex); // The channel is available again
    }
    pthread_mutex_unlock(&rtl_count_mutex);
}

// A function that simulates passenger boarding the plane
void Boarding(Passenger* passenger){
    pthread_mutex_lock(&boarding_check_mutex); // Only one person can board at a time

    PrintWithTime("Passenger " + passenger->Identity + " has started waiting to be boarded");
    passenger->HasBoardingPass = rand() % 3; // Randomly lose boarding pass

    // If passenger loses boarding pass
    if(passenger->HasBoardingPass == 0){
        pthread_mutex_lock(&print_mutex);
        cout << "Passenger " << passenger->Identity << " has lost boarding pass" << endl; 
        pthread_mutex_unlock(&print_mutex);
        pthread_mutex_unlock(&boarding_check_mutex); // He needs to return to special kiosk, so this area is open again
        return;
    }

    // Passenger has boarding pass, so board the plane
    PrintWithTime("Passenger " + passenger->Identity + " has started boarding the plane");
    sleep(Y);
    PrintWithTime("Passenger " + passenger->Identity + " has boarded the plane");

    passenger->BoardingComplete = 1; // Boarding complete for the passenger

    pthread_mutex_unlock(&boarding_check_mutex);
}

// A function that simulates the special kiosk in case passenger loses boarding pass
void SpecialKiosk(Passenger* passenger){
    PrintWithTime("Passenger " + passenger->Identity + " has arrived in front of special kiosk");

    pthread_mutex_lock(&special_kiosk_mutex); // Special kiosk can serve one person at a time

    // Do check up
    PrintWithTime("Passenger " + passenger->Identity + " has started self-check in special kiosk");
    sleep(W);
    PrintWithTime("Passenger " + passenger->Identity + " has finished self-check in special kiosk");
    
    pthread_mutex_unlock(&special_kiosk_mutex); // Check up in special kiosk done
}

/*-------------------------Thread Functions-------------------------*/

// Passenger Thread
void * PassengerProcess(void* arg_passenger){
    Passenger* passenger = (Passenger*) arg_passenger;
    // Self Check In Kiosk
    SelfCheckUp(passenger);

    // Non VIP need Security Check
    if(passenger->VIP == 0){
        SecurityBeltnonVIP(passenger);
    }else{
        // VIP Has special channel
        LeftToRight(passenger);
    }

    // Boarding
    while(true){
        Boarding(passenger);

        // Boarding done
        if(passenger->BoardingComplete == 1){
            break;
        }else{
            // Only possible way the boarding is not done is when the pass is lost, so, return to special kiosk
            RightToLeft(passenger);

            // Then do checkup in special kiosk
            SpecialKiosk(passenger);

            // Then return via VIP Channel
            LeftToRight(passenger);
        }
    }
    // Boarding done, safe journey 
    return (void *) 0;
}

// Passenger Producer
void * PassengerGenerator(void* argument){
    int Counter = 0;
    while(Counter < TOTAL_ARRIVALS){
        Passenger *passenger = new Passenger();
        passenger->CopyPassenger(AllPassenger[Counter]); // Had pointer issue so used copy

        PrintWithTime("Passenger " + passenger->Identity + " has arrived at airport");

        pthread_t PassengerThread;
        AllThreads[Counter] = PassengerThread;
        pthread_create(&AllThreads[Counter], NULL, PassengerProcess, (void*) passenger); // Create passenger thread
        
        // No need to sleep if it is the last passenger
        if(Counter != TOTAL_ARRIVALS - 1){
            int SleepTime = AllPassenger[Counter + 1].ArrivalTime - AllPassenger[Counter].ArrivalTime;
            //cout << Counter << ": " << SleepTime << endl;
            sleep(SleepTime);
        }
        Counter++;
    }

    for(int Counter=0; Counter<TOTAL_ARRIVALS; Counter++){
        pthread_join(AllThreads[Counter], NULL);
    }
    cout << "Simulation done for " << TOTAL_ARRIVALS << " passengers" << endl;
    if(OutputFile){
        OutputFile.close();
    }
    return (void *) 0;
}

/*-------------------------Initialization Functions-------------------------*/

// A function that reads the input file and initializes the variables
void InitializeVariables(){
    if(!fopen("input.txt", "r")){
        cout << "File not found" << endl;
        exit(-1);
    }
    fstream inputFile("input.txt");
    OutputFile.open("output.txt");
    if(!OutputFile){
        cout << "Cannot create output file, terminating" << endl;
        exit(-1);
    }
    
    // Get values of M, N and P
    inputFile >> M >> N >> P;
    // Get Values of W, X, Y and Z
    inputFile >> W >> X >> Y >> Z;
    inputFile.close();
}

// A function that initializes the necessary mutex and semaphores
void InitializeSemaphoresAndMutex(){
    // Kiosk
    sem_init(&kiosk_sem, 0, M);
    pthread_mutex_init(&print_mutex, NULL);
    pthread_mutex_init(&kiosk_check_mutex, NULL);
    kiosk_mutex = new pthread_mutex_t[M];
    for(int Counter=0; Counter<M; Counter++){
        pthread_mutex_init(&kiosk_mutex[Counter], NULL);
    }

    // Security Belt
    security_belt_sem = new sem_t[N];
    for(int Counter=0; Counter<N; Counter++){
        sem_init(&security_belt_sem[Counter], 0, P);
    }

    // Refined VIP
    pthread_mutex_init(&vip_way_mutex, NULL);
    pthread_mutex_init(&channel_mutex, NULL);
    pthread_mutex_init(&ltr_count_mutex, NULL);
    pthread_mutex_init(&rtl_count_mutex, NULL);

    // Boarding
    pthread_mutex_init(&boarding_check_mutex, NULL);

    // Special Kiosk
    pthread_mutex_init(&special_kiosk_mutex, NULL);
}

// A function that generates passenger arrival time based on poisson distribution
void PassengerArrivalInitialization(){
    srand(time(0));
    int TotalArrival = 0;

    double ArrivalRate = TOTAL_ARRIVALS / SIMULATION_TIME_MINUTES;
    double Lambda = 1.0 / ArrivalRate;

    default_random_engine RandomEngine;
    poisson_distribution<int> Poisson(Lambda);

    for(int Counter = 0; Counter < TOTAL_ARRIVALS; Counter++){
        int NewArrival = Poisson(RandomEngine);
        TotalArrival += NewArrival;
        Passenger passenger;
        passenger.PassengerID = Counter;
        passenger.ArrivalTime = TotalArrival;
        passenger.VIP = rand()%2; // Randomly assign VIP Status

        if(passenger.VIP == 1){
            passenger.Identity = to_string(passenger.PassengerID) + "(VIP)";
        }else{
            passenger.Identity = to_string(passenger.PassengerID);
        }

        AllPassenger[Counter] = passenger;
    }
}

// A function that initializes time
void InitializeCurrentTime(){
    FirstPassengerTime = AllPassenger[0].ArrivalTime;
    StartTime = steady_clock::now();
}

// A function that initializes required steps, might not be necessary though
void InitializeSteps(){
    Kiosk = new int[M];
    for(int i=0; i<M; i++){
        Kiosk[i] = 1;
    }
}

// A function that handles all initializations
void InitializeProgram(){
    InitializeVariables();
    InitializeSemaphoresAndMutex();
    PassengerArrivalInitialization();
    InitializeCurrentTime();
    InitializeSteps();
}


/*-------------------------Main Function-------------------------*/

int main(void){
    InitializeProgram();

    pthread_t GeneratorThread;
    pthread_create(&GeneratorThread, NULL, PassengerGenerator, NULL);

    pthread_join(GeneratorThread, NULL);
	return 0;
}