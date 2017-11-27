#include <iomanip>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <exception>

#include <atlstr.h>

#include "ieventcounter.h"


struct CIEquality
{
   bool
   operator()(
      int c1,
      int c2) const
   {
      return ::toupper(c1) == ::toupper(c2);
   }
};


bool
CIEqualStrings(
   const std::string & crStr1,
   const std::string & crStr2)
{
   return ((crStr1.length() == crStr2.length()) &&
           std::equal(crStr1.begin(),
                      crStr1.end(),
                      crStr2.begin(),
                      CIEquality()));
}
//------------------------------------------------------------------------------


// Originally implemented as a lambda expression. The function, however, grew
// to the point that it looked messy as a lambda.

void
ThreadFunction(
   CString deviceID,
   const std::string & crStrLogName,
   IEventCounter * pEventCounter,
   int * const pCount)
{
#ifdef USE_EXCEPTIONS
   try
   {
      // 'ParseEvents(...)' does not return a value, so it will toss a
      // 'ParseEventException' exception if a serious error is encountered.
      pEventCounter->ParseEvents(deviceID, crStrLogName.c_str());
      *pCount = pEventCounter->GetEventCount(deviceID);
      return;
   }
   catch (const ParseEventException & crExc)
   {
      std::cerr << "Device: " << deviceID << std::endl
                << crExc.what() << std::endl << std::endl;
   }
   catch (const std::exception & crExc)
   {
      std::cerr << "Device: " << deviceID << std::endl
                << crExc.what() << std::endl << std::endl;
   }

   *pCount = -1;
   return;
#else
   pEventCounter->ParseEvents(deviceID, crStrLogName.c_str());
   *pCount = pEventCounter->GetEventCount(deviceID);
   return;
#endif
}
//------------------------------------------------------------------------------

void
Usage(
   const std::string & crStrAppName,
   std::string & crStrMessage)
{
   // Perhaps I should have tried a HERE document.

   std::cout << std::endl << crStrMessage << std::endl << std::endl
             << "Minimal usage:" << std::endl
             << crStrAppName
             << " <# of threads to use> <device ID> <log file name>"
             << std::endl << "If desired, additional device ID and log file "
             << "name pairs may be supplied." << std::endl
             << "(The order shown above must be honoured.)"
             << std::endl << std::endl
             << "The number of threads requested must be at least as large "
             << "as the number of device ID/log file " << std::endl
             << "name pairs. If the number of threads exceeds the number of "
             << "pairs, at least some of the " << std::endl
             << "pairs will be executed by more than one thread."
             << std::endl << std::endl;
   return;
}
//------------------------------------------------------------------------------


int
main(
   int argc,
   char * argv[])
{
   // Ordinarily would use command line parser, but I cannot find one supplied
   // with VS 2017. Oh well.
   if ((argc > 1) &&
      (CIEqualStrings("-help", argv[1]) ||
       CIEqualStrings("-h", argv[1]) ||
       CIEqualStrings("--help", argv[1])))
   {
      Usage(argv[0], std::string("Help requested."));
      return 0;
   }
   else if (argc < 4)                  // Too few arguments?
   {
      Usage(argv[0], std::string("Too few arguments."));
      return 1;
   }

   if ((argc % 2) == 1)                // Odd number of arguments?
   {
      Usage(argv[0],
            std::string("The number of command-line arguments is wrong."));
      return 1;
   }

   int numThreads = 0;

   try
   {
      numThreads = std::stoi(argv[1]);
   }
   catch (const std::invalid_argument & crExc)
   {
      std::cerr << "An invalid value for the number of threads: "
                << std::endl << crExc.what() << std::endl;
      return 1;
   }
   catch (const std::out_of_range & crExc)
   {
      std::cerr << "An out-of-range value for the number of threads: "
                << std::endl << crExc.what() << std::endl;
      return 1;
   }

   if (numThreads < ((argc - 2) / 2))
   {
      Usage(argv[0],
            std::string("The number of threads must be at least as large " \
                        "as the number of device ID/log file name pairs."));
      return 1;
   }

   std::vector<std::pair<CString, std::string>> vecDeviceInfo;
   std::vector<std::thread> vecThreads;

   for (int i = 2; i < argc; i = i + 2)
      vecDeviceInfo.emplace_back(CString(argv[i]), std::string(argv[i + 1]));

   IEventCounter eCounter;
   std::vector<int> vecCounts(numThreads, -1);

   for (size_t i = 0; i < static_cast<size_t>(numThreads); ++i)
   {
      vecThreads.push_back(std::thread(&ThreadFunction,
                                       vecDeviceInfo.at(i % vecDeviceInfo.size()).first,
                                       vecDeviceInfo.at(i % vecDeviceInfo.size()).second,
                                       &eCounter,
                                       &vecCounts[i]));
   }

   for (auto & aThread : vecThreads)
      aThread.join();

   for (size_t i = 0; i < vecCounts.size(); ++i)
   {
      std::cout << "For device "
                << vecDeviceInfo.at(i % vecDeviceInfo.size()).first
                << ", log file "
                << vecDeviceInfo.at(i % vecDeviceInfo.size()).second
                << ", the faulty sequence count == " << vecCounts.at(i)
                << std::endl;
   }

   return 0;
}