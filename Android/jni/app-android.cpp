#include "Globals.h"

#include <jni.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>

#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <assert.h>

#include "OSSupport/CriticalSection.h"
#include "OSSupport/File.h"
#include "OSSupport/NetworkSingleton.h"
#include "ToJava.h"

#include "Root.h"
#include "WebAdmin.h"

#include <android/log.h>

#ifdef _WIN32 // For IntelliSense parsing
typedef void jobject;
typedef int jint;
typedef bool jboolean;
typedef void JavaVM;
typedef void JNIEnv;
#endif

cCriticalSection g_CriticalSection;

JNIEnv* g_CurrentJNIEnv = 0;
jobject g_JavaThread = 0;
JavaVM* g_JavaVM = 0;




class cRootLauncher
{
public:

	void Start()
	{
		m_Root.Start();
	}

	void Stop()
	{
		if (!IsStopped())
		{
			m_Root.QueueExecuteConsoleCommand("stop");
		}
	}

	bool IsStopped()
	{
		return m_Root.GetServer() == nullptr;
	}

	cRoot & GetInstance()
	{
		return m_Root;
	}

private:

	cRoot m_Root;

} RootLauncher;





extern "C"
{
	jint JNI_OnLoad(JavaVM* vm, void* reserved)
	{
		g_JavaVM = vm;
		return JNI_VERSION_1_4;
	}

	/* Called when program/activity is created */
	JNIEXPORT void JNICALL Java_com_mcserver_MCServerActivity_NativeOnCreate(JNIEnv*  env, jobject thiz)
	{
		g_CriticalSection.Lock();
		g_CurrentJNIEnv = env;
		g_JavaThread = thiz;
		g_CriticalSection.Unlock();

		cFile::CreateFolder(AString(FILE_IO_PREFIX) + "mcserver");

		// Initialize logging subsystem:
		cLogger::InitiateMultithreading();

		// Initialize LibEvent:
		cNetworkSingleton::Get();

		try
		{
			RootLauncher.Start();
		}
		catch (std::exception & e)
		{
			LOGERROR("Standard exception: %s", e.what());
		}
		catch (...)
		{
			LOGERROR("Unknown exception!");
		}

		// Shutdown all of LibEvent:
		cNetworkSingleton::Get().Terminate();
	}





	JNIEXPORT void JNICALL Java_com_mcserver_MCServerActivity_NativeCleanUp(JNIEnv*  env, jobject thiz)
	{
		g_CriticalSection.Lock();
		g_CurrentJNIEnv = env;
		g_JavaThread = thiz;
		g_CriticalSection.Unlock();

		RootLauncher.Stop();
	}





	JNIEXPORT jboolean JNICALL Java_com_mcserver_MCServerActivity_NativeIsServerRunning(JNIEnv* env, jobject thiz)
	{
		return !RootLauncher.IsStopped();
	}





	JNIEXPORT jint JNICALL Java_com_mcserver_MCServerActivity_NativeGetWebAdminPort(JNIEnv* env, jobject thiz)
	{
		if (!RootLauncher.IsStopped() && (RootLauncher.GetInstance().GetWebAdmin() != nullptr))
		{
			return std::atoi(RootLauncher.GetInstance().GetWebAdmin()->GetIPv4Ports().c_str());
		}
		return 0;
	}
}