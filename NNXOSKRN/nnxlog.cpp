#include "nnxlog.h"
#include "text.h"
#include <MemoryOperations.h>

NNXLogger* gLogger = 0;

void NNXLogger::Log(const char* text, va_list l)
{
	UINT64 index, textLength = FindCharacterFirst((char*) text, -1, 0);
	char numberBuffer[65];
	void* args = (void*) l;

	if (textLength == 0)
		return;

	for (index = 0; index < textLength;)
	{
		if (text[index] == '%')
		{
			index++;
			char nextCharacter = text[index++];
			switch (nextCharacter)
			{
				case '%':
				{
					this->AppendText("%", 1);
					break;
				}
				case 'i':
				case 'd':
				case 'x':
				case 'X':
				case 'u':
				case 'b':
				{
					INT64 i = *((INT64*) args);
					args = ((INT64*) args) + 1;
					if (nextCharacter == 'X')
						IntegerToAsciiCapital(i, 16, numberBuffer);
					else if (nextCharacter == 'x')
						IntegerToAscii(i, 16, numberBuffer);
					else if (nextCharacter == 'b')
					{
						IntegerToAscii(i, 2, numberBuffer);
					}
					else if (nextCharacter == 'u')
						IntegerToAscii(i, -8, numberBuffer);
					else
						IntegerToAscii(i, -10, numberBuffer);

					this->AppendText(numberBuffer, FindCharacterFirst(numberBuffer, -1, 0));
					break;
				}
				case 'c':
				{
					char i[2];
					i[0] = (*((INT64*) args)) & 0xFF;
					i[1] = 0;
					args = ((INT64*) args) + 1;
					this->AppendText(i, 1);
					break;
				}
				case 's':
				{
					char* str = *((char**) args);
					args = ((INT64*) args) + 1;
					this->AppendText(str, FindCharacterFirst(str, -1, 0));
					break;
				}
				case 'S':
				{
					UINT64 len;
					char* str = *((char**) args);
					args = ((INT64*) args) + 1;
					len = *((UINT64*) args);
					args = ((INT64*) args) + 1;
					this->AppendText(str, len);
					break;
				}
				default:
					break;
			}
		}
		else
		{
			UINT64 nextPercent = FindCharacterFirst((char*) text + index, textLength - index, '%');
			if (nextPercent == -1)
			{
				nextPercent = textLength - index;
			}

			this->AppendText(text + index, nextPercent);
			index += nextPercent;
		}
	}
}

void NNXLogger::AppendText(const char* text, UINT64 textLength)
{
	for (int i = 0; i < textLength; i++)
	{
		PrintT("%c", text[i]);
	}

	if (position + textLength >= 512)
	{
		this->Flush();
	}

	MemCopy(buffer + position, (void*) text, textLength);
	position += textLength;
	buffer[position] = 0;
}

void NNXLogger::Flush()
{
	this->filesystem->Functions.AppendFile(this->loggerFile, this->position, this->buffer);
	this->position = 0;
}

NNXLogger::NNXLogger(VFS_FILE* file)
{
	this->loggerFile = file;
	this->filesystem = file->Filesystem;
	buffer = new unsigned char[512];
	position = 0;
}

void NNXLogger::Log(const char* str, ...)
{
	va_list args;
	va_start(args, str);
	this->Log(str, args);
	va_end(args);
}

void NNXLogger::Clear()
{
	this->filesystem->Functions.DeleteFile(this->loggerFile);
	this->filesystem->Functions.RecreateDeletedFile(this->loggerFile);
	this->position = 0;
}

NNXLogger::~NNXLogger()
{
	this->Flush();
	this->filesystem->Functions.CloseFile(this->loggerFile);
}

extern "C" void NNXLogG(const char* str, ...)
{
	if (gLogger == nullptr)
	{
		PrintT("Logger not ready\n");
		return;
	}
	va_list args;
	va_start(args, str);
	gLogger->Log(str, args);
	va_end(args);
}

extern "C" void NNXClearG()
{
	if (gLogger == nullptr)
	{
		PrintT("Logger not ready\n");
		return;
	}
	gLogger->Clear();
}

extern "C" void NNXSetLoggerG(void* input)
{
	if (gLogger == nullptr)
	{
		PrintT("Logger not ready\n");
		return;
	}
	if (gLogger)
		delete gLogger;
	gLogger = (NNXLogger*) input;
}

extern "C" void* NNXGetLoggerG()
{
	return gLogger;
}

extern "C" void* NNXNewLoggerG(VFS_FILE* file)
{
	NNXSetLoggerG(new NNXLogger(file));
	return (void*) NNXGetLoggerG();
}

extern "C" void NNXLoggerTest(VFS* filesystem)
{
	VFS_FILE* loggerFile;
	if (filesystem->Functions.CheckIfFileExists(filesystem, (char*)"LOG.TXT"))
	{
		if (filesystem->Functions.DeleteAndCloseFile(filesystem->Functions.OpenFile(filesystem, (char*)"LOG.TXT")))
		{
			PrintT("Cannot delete old log\n");
			return;
		}
	}

	if (filesystem->Functions.CreateFile(filesystem, (char*)"LOG.TXT"))
	{
		PrintT("Cannot create file\n");
		return;
	}

	loggerFile = filesystem->Functions.OpenFile(filesystem, (char*)"LOG.TXT");
	if (loggerFile)
	{
		if (gLogger)
			delete gLogger;
		gLogger = new NNXLogger(loggerFile);
	}
}