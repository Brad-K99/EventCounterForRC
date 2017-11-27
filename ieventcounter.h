#include <atlstr.h>
#include <string>
#include <map>
#include <ctime>
#include <shared_mutex>

#ifdef USE_EXCEPTIONS
#include <exception>

class ParseEventException : public std::exception
{
   public:

      ParseEventException::ParseEventException(const char * const cpcMessage) :
         exception(cpcMessage)
      { };
};
#endif

// Originally wrote this as a nested class, but it does not require access
// to anything in IEventCounter, so let it reside outside that class.

class FaultSequenceInfo
   {
   private:

      short m_sequenceNum;
      short m_lastStage;
      bool m_firstFaultyStage;
      time_t m_lastEpochTime;

      bool
      MoreThan5Minutes(const time_t cCurrentTime)
      {
         return ((cCurrentTime - m_lastEpochTime) >= 300) ? true : false;
      }
      //------------------------------------------------------------------

      void
      Reset()
      {
         m_sequenceNum = 0;
         m_lastStage = -1;
         m_firstFaultyStage = false;
         m_lastEpochTime = 0;
      }
      //------------------------------------------------------------------

   public:

      FaultSequenceInfo() :
         m_sequenceNum(0), m_lastStage(-1), m_firstFaultyStage(false),
         m_lastEpochTime(0)
      { };

      bool
      FaultySequenceCompleted(
         const short cStage,
         const time_t cEpochTime);
};


class IEventCounter
{
   private:

      const static std::string m_strFaultPattern;
      // Map of device IDs to the number of faulty patterns.
      std::map<CString, int> m_mapIdToCount;
      std::map<CString, std::shared_mutex> m_mapIdToMutex;

      bool
      IEventCounter::ParseAndTestLine(
         const CString cDeviceID,
         const char * const cpcLogName,
         const std::string & crStrLine,
         std::map<CString, int>::iterator iter,
         FaultSequenceInfo * const cpSeqInfo);

   public:

      void ParseEvents(CString deviceID, const char * logName);
      int GetEventCount(CString deviceId);
};

