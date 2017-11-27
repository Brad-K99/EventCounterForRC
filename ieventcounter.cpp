#include "ieventcounter.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <locale>
#include <iomanip>
#include <ctime>
#include <vector>
#include <utility>
#include <string>


// This could be in the class, but does not need to be. Most likely one would
// keep this in a library of utility functions.

bool
GenerateEpochTime(
   const std::string & crStrDate,
   const std::string & crStrTime,
   time_t * const cpEpochTime)
{
   std::tm seconds = {};
   std::istringstream ss(crStrDate + "T" + crStrTime + "Z");

   if (ss >> std::get_time(&seconds, "%Y-%m-%dT%H:%M:%S"))
   {
      *cpEpochTime = std::mktime(&seconds);
   }
   else
      return false;

    return true;
}
//------------------------------------------------------------------------------


// This could be in the class, but does not need to be. Most likely one would
// keep this in a library of utility functions.

void
SplitStringByDelimiter(
   const std::string & crStrLine,
   const char cDelimiter,
   std::vector<std::string> * cpVecDTComponents)
{
   std::string strPiece;
   std::istringstream ss(crStrLine);

   while (std::getline(ss, strPiece, cDelimiter))
      cpVecDTComponents->push_back(strPiece);

   return;
}
//------------------------------------------------------------------------------

bool
FaultSequenceInfo::FaultySequenceCompleted(
   const short cStage,
   const time_t cEpochTime)
{
   // A switch statement may have been cleaner.

   if (cStage == 0)              // In stage 0?
   {
      // If the sequence number was 2 or 3, we have just found a faulty
      // sequence; if the sequence number was something else, we
      // are not in a faulty sequence.
      return (m_sequenceNum >= 2) ? (Reset(), true) : (Reset(), false);
   }
   else if (cStage == 1)         // In stage 1?
   {
      // Not in a faulty sequence.
      Reset();
   }
   else if (cStage == 2)         // In stage 2?
   {
      // Previous stage was 2 or 3 and the sequence number is
      // 2 or 3?
      if ((m_lastStage >= 2) &&
         ((m_sequenceNum == 2) || (m_sequenceNum == 3)))
      {
         m_sequenceNum = 3;
         m_lastStage = cStage;
      }
      // Transitioned to stage 2 from stage 3, but the sequence number
      // is neither 2 nor 3?
      else if (m_lastStage == 3)
      {
         // Have we encountered sequence 1?
         if (MoreThan5Minutes(cEpochTime))
         {
            // Because we just entered stage 2, we are in sequence 2.
            m_sequenceNum = 2;
            m_lastEpochTime = 0;    // No longer important.
            m_lastStage = cStage;
         }
         else
         {
            // Less than 5 minutes for stage 3, so we have not started
            // a faulty sequence.
            Reset();
         }
      }
   }
   else if (cStage == 3)         // In stage 3?
   {
      // Previous stage was 2 or 3 and the sequence number is
      // 2 or 3?
      if ((m_lastStage >= 2) &&
         ((m_sequenceNum == 2) || (m_sequenceNum == 3)))
      {
         m_sequenceNum = 3;
      }
      // Are we potentially starting a new faulty sequence?
      else if (m_lastStage <= 0)
      {
         // We must preserve this information in case this stage lasts
         // 5 minutes or more.
         m_sequenceNum = 1;
         m_lastEpochTime = cEpochTime;
      }

      m_lastStage = cStage;
   }

   return false;
}
//------------------------------------------------------------------------------


void
IEventCounter::ParseEvents(
   CString deviceID,
   const char * logName)
{
   // Nearly all of this method (and the methods/functions it calls) constitute
   // a critical section. Writing the count for this device should be
   // restricted to one thread at a time. Also, see the comment about the
   // 'unique_lock' below.

   // Create an entry in the map for this device ID. If one already exists,
   // that's fine. Evidently, if an exception is thrown by this method, the
   // map will be left unchanged.
   // See http://www.cplusplus.com/reference/map/map/emplace/.
   auto aPairIdToMutex =
      m_mapIdToMutex.emplace(std::piecewise_construct,
                             std::make_tuple(deviceID),
                             std::make_tuple());
   // We do NOT want everyone and their dog reading the log file simultaneously.
   // Excessive read requests will impede Windows. The 'unique_lock' instance
   // will be destroyed (releasing the lock on the mutex) when the method
   // terminates. This should hold even if an exception is tossed. See
   // http://www.cplusplus.com/reference/mutex/unique_lock/.
   // Thank you stack unwinding.
   std::unique_lock<std::shared_mutex> uLock(aPairIdToMutex.first->second);
   std::string strLine;
   std::ifstream inFile(logName);

   if (!inFile.is_open())
   {
#ifdef USE_EXCEPTIONS
      throw ParseEventException(
         (std::string("Error opening the file.\n")).c_str());
#else
      return;
#endif
   }

   FaultSequenceInfo sequenceInfo;
   // Create an entry in the map for this device ID. Assume failure.
   auto aPair = m_mapIdToCount.insert(std::make_pair(deviceID, -1));

   // If an entry for this device already existed, set the count to -1.
   // Presumably 'insert_or_assign(...)' could have been used instead, but that
   // method requires C++17.
   if (aPair.second == false)
      aPair.first->second = -1;

   // Read every line until the file is exhausted. Note that the testing for
   // file errors is NOT comprehensive!
   while (getline(inFile, strLine))
   {
      if (!ParseAndTestLine(deviceID,
                            logName,
                            strLine,
                            aPair.first,
                            &sequenceInfo))
      {
         inFile.close();
#ifdef USE_EXCEPTIONS
         throw ParseEventException(
            (std::string("An exception was encountered during an attempt " \
               "to examine a line in the file.\n")).c_str());
#else
         return;
#endif
      }
   }

   inFile.close();
   // Because unique_lock follows the RAII pattern, we need not explicitly
   // unlock the mutex.
   return;
}
//------------------------------------------------------------------------------


int
IEventCounter::GetEventCount(CString deviceId)
{
   auto iterIdToCount = m_mapIdToCount.find(deviceId);
   auto iterIdToMutex = m_mapIdToMutex.find(deviceId);

   // If true, presumably this method has been called before 'ParseEvents(...)'.
   // Treat this as an error. (We could defer seeking the "count" iterator
   // until after the mutex is found, preventing the use of the 'find(...)' if
   // the "mutex" iterator is not found. But that would mean testing for the
   if ((iterIdToCount == m_mapIdToCount.end()) ||
       (iterIdToMutex == m_mapIdToMutex.end()))
   {
#ifdef ERROR_REPORTING
      std::cerr << "A count for device " << deviceId << " is unavailable."
                << std::endl << "Was 'ParseEvents(...)' called for this device?"
                << std::endl;
#endif
      return -1;        // Let -1 signal an error the calling function.
   }

   // Multiple readers is permissible. This should block only if another thread
   // is executing 'ParseEvents(...)' for this device. Note that
   // 'ParseEvents(...)' (the 'writer') requires exclusive access to this mutex;
   // overuse of the current method (the 'reader') for a device may starve the
   // writer.
   std::shared_lock<std::shared_mutex> sLock(iterIdToMutex->second);

   if (iterIdToCount != m_mapIdToCount.end())
      return iterIdToCount->second;

#ifdef ERROR_REPORTING
   std::cerr << "A count of the faulty sequence events for the device "
             << deviceId << " does NOT exist!" << std::endl
             << "Try calling 'ParseEvents(...) first." << std::endl;
#endif
   return -1;           // Let -1 signal an error the calling function.
}
//------------------------------------------------------------------------------


bool
IEventCounter::ParseAndTestLine(
   const CString cDeviceID,
   const char * const cpcLogName,
   const std::string & crStrLine,
   std::map<CString, int>::iterator iter,
   FaultSequenceInfo * const cpSeqInfo)
{
   std::vector<std::string> vecDTComponents;
   // Problem description indicates that each line consists of a date, time, and
   // value, with the date and time separated by a space, and the time and value
   // separated by a tab.
   SplitStringByDelimiter(crStrLine, '\t', &vecDTComponents);

   if (vecDTComponents.size() != 2)
   {
#ifdef ERROR_REPORTING
      std::cerr << "The line '" << crStrLine
                << "' is NOT valid! Expecting a format of YYYY-MM-DD "
                << "HH:mm:SS\t#." << std::endl;
#endif
      return false;
   }

   short stage = stoi(vecDTComponents.back());  // Save the current stage.

   if ((stage < 0) || (stage > 3))
   {
#ifdef ERROR_REPORTING
      std::cerr << "The line '" << crStrLine
                << "' is NOT valid! Expecting a stage ranging from 0 to 3."
                << std::endl;
#endif
      return false;
   }

   // Preserve the date & time strings before tokenizing.
   const std::string cStrDT(vecDTComponents.front());

   // Empty the vector so that the components will occupy the 1st 2 elements.
   vecDTComponents.clear();
   SplitStringByDelimiter(cStrDT, ' ', &vecDTComponents);

   if (vecDTComponents.size() != 2)
   {
#ifdef ERROR_REPORTING
      std::cerr << "The line '" << crStrLine << "' is NOT valid! " << std::endl
                << "Expecting date & time in the format YYYY-MM-DD HH:mm:SS."
                << std::endl;
#endif
      return false;
   }

   time_t epochTime;

   if (!GenerateEpochTime(vecDTComponents.front(), vecDTComponents.back(), &epochTime))
   {
#ifdef ERROR_REPORTING
      std::cerr << "Cannot generate epoch time!" << std::endl;
#endif
      return false;
   }

   if (cpSeqInfo->FaultySequenceCompleted(stage, epochTime))
   {
      if (iter->second < 0)
         iter->second = 1;
      else
         ++iter->second;
   }

   // Zero faulty sequences were found? We initialized the count to -1 (error
   // state). An error has not been encountered, so reset the count to 0.
   if (iter->second < 0)
      iter->second = 0;

   return true;
}
//------------------------------------------------------------------------------
