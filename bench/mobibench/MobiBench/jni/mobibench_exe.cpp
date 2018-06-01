#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <android/log.h>

#ifdef __cplusplus
extern "C" {
#endif
extern int main( int argc, char **argv);
extern int progress;
extern float cpu_active;
extern float cpu_idle;
extern float cpu_iowait;
extern int cs_total;
extern int cs_voluntary;
extern float throughput;
extern float tps;
extern int g_state;

#define printf(fmt,args...)  __android_log_print(4  ,NULL, fmt, ##args)

void mobibench_run(JNIEnv* env, jobject obj, jstring string);
int getMobibenchProgress(JNIEnv* env, jobject obj);
int getMobibenchState(JNIEnv* env, jobject obj);

JNIEXPORT jint JNI_OnLoad(JavaVM* vm, void* reserved)
{
	JNIEnv* env = NULL;
	JNINativeMethod nm[3];
	jclass cls;
	jint result = -1;

	printf("%s\n", __func__);

	if(vm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK)
	{
		printf("JNI version Error");
		return JNI_ERR;
	}

	cls = env->FindClass("esos/MobiBench/MobiBenchExe");

	nm[0].name = "mobibench_run";
	nm[0].signature = "(Ljava/lang/String;)V";
	nm[0].fnPtr = (void*)mobibench_run;

	nm[1].name = "getMobibenchProgress";
	nm[1].signature = "()I";
	nm[1].fnPtr = (void*)getMobibenchProgress;

	nm[2].name = "getMobibenchState";
	nm[2].signature = "()I";
	nm[2].fnPtr = (void*)getMobibenchState;

	env->RegisterNatives(cls, nm, 3);

	return JNI_VERSION_1_6;
}

int getMobibenchProgress(JNIEnv* env, jobject obj)
{
//	printf("%s\n", __func__);

	return progress;
}

int getMobibenchState(JNIEnv* env, jobject obj)
{
	return g_state;
}

int arg(char** argv, char* command)
{
    char *tok;
    int count = 0;

    tok = strtok(command, " \n\r\n\t");

    while (tok)
    {
        argv[count] = (char*)malloc(sizeof(char) * 128);
        strcpy(argv[count], tok);
        tok = strtok(NULL, " \n\r\n\t");
        count++;
    }

    return count;
}

void mobibench_run(JNIEnv* env, jobject obj, jstring string)
{
	char** argv;
	int argc;
	const char *str = env->GetStringUTFChars(string, 0);
	printf("%s\n", str);
	jfieldID fid;
	jclass cls = env->FindClass("esos/MobiBench/MobiBenchExe");

	argv = (char**)malloc(sizeof(char) * 1024);
	argc = arg(argv, (char *)str);

	main(argc, argv);

	fid = env->GetFieldID(cls, "cpu_active", "F");
	env->SetFloatField(obj, fid, cpu_active);

	fid = env->GetFieldID(cls, "cpu_idle", "F");
	env->SetFloatField(obj, fid, cpu_idle);

	fid = env->GetFieldID(cls, "cpu_iowait", "F");
	env->SetFloatField(obj, fid, cpu_iowait);

	fid = env->GetFieldID(cls, "cs_total", "I");
	env->SetIntField(obj, fid, cs_total);

	fid = env->GetFieldID(cls, "cs_voluntary", "I");
	env->SetIntField(obj, fid, cs_voluntary);

	fid = env->GetFieldID(cls, "tps", "F");
	env->SetFloatField(obj, fid, tps);

	fid = env->GetFieldID(cls, "throughput", "F");
	env->SetFloatField(obj, fid, throughput);

	for(int i = 0; i < argc; i++) {
		free(argv[i]);
	}

	free(argv);
}

#ifdef __cplusplus
}
#endif
