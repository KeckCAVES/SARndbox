/***********************************************************************
BathymetrySaverTool - Tool to save the current bathymetry grid of an
augmented reality sandbox to a file or network socket.
Copyright (c) 2016 Oliver Kreylos

This file is part of the Augmented Reality Sandbox (SARndbox).

The Augmented Reality Sandbox is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Augmented Reality Sandbox is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Augmented Reality Sandbox; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
***********************************************************************/

#include "BathymetrySaverTool.h"

#include <stdexcept>
#include <iomanip>
#include <Misc/PrintInteger.h>
#include <Misc/ThrowStdErr.h>
#include <Misc/MessageLogger.h>
#include <Misc/StandardValueCoders.h>
#include <Misc/ConfigurationFile.h>
#include <IO/ValueSource.h>
#include <IO/OStream.h>
#include <Comm/TCPPipe.h>
#include <Math/Math.h>
#include <Vrui/OpenFile.h>

#include "WaterTable2.h"
#include "Sandbox.h"

/**********************************************************
Methods of class BathymetrySaverToolFactory::Configuration:
**********************************************************/

BathymetrySaverToolFactory::Configuration::Configuration(void)
	:saveFileName("BathymetrySaverTool.dem"),
	 postUpdate(false),postUpdatePort(80),postUpdatePage(""),
	 postUpdateMessage("app.GenerateTileCache();"),
	 gridScale(1.0)
	{
	}

void BathymetrySaverToolFactory::Configuration::read(const Misc::ConfigurationFileSection& cfs)
	{
	saveFileName=cfs.retrieveString("./saveFileName",saveFileName);
	postUpdate=cfs.retrieveValue<bool>("./postUpdate",postUpdate);
	postUpdateHostName=cfs.retrieveString("./postUpdateHostName",postUpdateHostName);
	postUpdatePort=cfs.retrieveValue<int>("./postUpdatePort",postUpdatePort);
	postUpdatePage=cfs.retrieveString("./postUpdatePage",postUpdatePage);
	postUpdateMessage=cfs.retrieveString("./postUpdateMessage",postUpdateMessage);
	gridScale=cfs.retrieveValue<double>("./gridScale",gridScale);
	}

void BathymetrySaverToolFactory::Configuration::write(Misc::ConfigurationFileSection& cfs) const
	{
	cfs.storeString("./saveFileName",saveFileName);
	cfs.storeValue<bool>("./postUpdate",postUpdate);
	cfs.storeString("./postUpdateHostName",postUpdateHostName);
	cfs.storeValue<int>("./postUpdatePort",postUpdatePort);
	cfs.storeString("./postUpdatePage",postUpdatePage);
	cfs.storeString("./postUpdateMessage",postUpdateMessage);
	cfs.storeValue<double>("./gridScale",gridScale);
	}

/*******************************************
Methods of class BathymetrySaverToolFactory:
*******************************************/

BathymetrySaverToolFactory::BathymetrySaverToolFactory(WaterTable2* sWaterTable,Vrui::ToolManager& toolManager)
	:ToolFactory("BathymetrySaverTool",toolManager),
	 waterTable(sWaterTable)
	{
	/* Retrieve bathymetry grid and cell sizes: */
	for(int i=0;i<2;++i)
		{
		gridSize[i]=waterTable->getBathymetrySize(i);
		cellSize[i]=waterTable->getCellSize()[i];
		}
	
	/* Initialize tool layout: */
	layout.setNumButtons(1);
	
	#if 0
	/* Insert class into class hierarchy: */
	ToolFactory* toolFactory=toolManager.loadClass("Tool");
	toolFactory->addChildClass(this);
	addParentClass(toolFactory);
	#endif
	
	/* Load class settings: */
	Misc::ConfigurationFileSection cfs=toolManager.getToolClassSection(getClassName());
	configuration.read(cfs);
	
	/* Set tool class' factory pointer: */
	BathymetrySaverTool::factory=this;
	}

BathymetrySaverToolFactory::~BathymetrySaverToolFactory(void)
	{
	/* Reset tool class' factory pointer: */
	BathymetrySaverTool::factory=0;
	}

const char* BathymetrySaverToolFactory::getName(void) const
	{
	return "Save Bathymetry";
	}

const char* BathymetrySaverToolFactory::getButtonFunction(int) const
	{
	return "Save Bathymetry";
	}

Vrui::Tool* BathymetrySaverToolFactory::createTool(const Vrui::ToolInputAssignment& inputAssignment) const
	{
	return new BathymetrySaverTool(this,inputAssignment);
	}

void BathymetrySaverToolFactory::destroyTool(Vrui::Tool* tool) const
	{
	delete tool;
	}

/********************************************
Static elements of class BathymetrySaverTool:
********************************************/

BathymetrySaverToolFactory* BathymetrySaverTool::factory=0;

/************************************
Methods of class BathymetrySaverTool:
************************************/

namespace {

/****************
Helper functions:
****************/

std::ostream& printInt2(std::ostream& os,int value)
	{
	os<<std::setw(6)<<value;
	return os;
	}

std::ostream& printFloat4(std::ostream& os,double value)
	{
	if(value!=0.0)
		{
		/* Split the value into mantissa and exponent: */
		int exponent=int(Math::floor(Math::log10(Math::abs(value))));
		double mantissa=value/Math::pow(10.0,double(exponent));
		
		/* Write the number: */
		std::ios::fmtflags oldFlags=os.flags(std::ios::showpoint|std::ios::dec|std::ios::fixed|std::ios::right);
		char oldFill=os.fill(' ');
		std::streamsize oldPrecision=os.precision(5);
		os<<std::setw(7)<<mantissa<<'e';
		os.setf(std::ios::showpos);
		os.fill('0');
		os<<std::internal<<std::setw(4)<<exponent;
		os.flags(oldFlags);
		os.fill(oldFill);
		os.precision(oldPrecision);
		}
	else
		os<<"0.00000e+000";
	
	return os;
	}

std::ostream& printFloat8(std::ostream& os,double value)
	{
	if(value!=0.0)
		{
		/* Split the value into mantissa and exponent: */
		int exponent=int(Math::floor(Math::log10(Math::abs(value))));
		double mantissa=value/Math::pow(10.0,double(exponent));
		
		/* Write the number: */
		std::ios::fmtflags oldFlags=os.flags(std::ios::showpoint|std::ios::dec|std::ios::fixed|std::ios::right);
		char oldFill=os.fill(' ');
		std::streamsize oldPrecision=os.precision(15);
		os<<std::setw(19)<<mantissa<<'D';
		os.setf(std::ios::showpos);
		os.fill('0');
		os<<std::internal<<std::setw(4)<<exponent;
		os.flags(oldFlags);
		os.fill(oldFill);
		os.precision(oldPrecision);
		}
	else
		os<<"  0.000000000000000D+000";
	
	return os;
	}

}

void BathymetrySaverTool::writeDEMFile(void) const
	{
	/* Open the output file as a std::ostream: */
	IO::OStream demFile(Vrui::openFile(configuration.saveFileName.c_str(),IO::File::WriteOnly));
	
	/* Write the bathymetry name: */
	static const char* fileHeader="Augmented Reality Sandbox bathymetry grid";
	demFile<<fileHeader;
	for(size_t i=strlen(fileHeader);i<144;++i)
		demFile<<' ';
	
	/* Write first part of header: */
	printInt2(demFile,1); // DEM level code (DEM-1)
	printInt2(demFile,1); // Elevation pattern (regular)
	printInt2(demFile,1); // Planimetric reference system code (UTM)
	printInt2(demFile,10); // Planimetric reference system zone (Northern California)
	
	/* Write dummy map projection parameters, because UTM: */
	for(int i=0;i<15;++i)
		printFloat8(demFile,0.0);
	
	/* Write units of measurement: */
	printInt2(demFile,2); // Horizontal unit is meters
	printInt2(demFile,2); // Vertical unit is meters
	
	/* Retrieve the grid scale factor: */
	double gs=configuration.gridScale;
	
	/* Write the DEM coverage polygon: */
	printInt2(demFile,4); // Polygon is quadrangle
	
	/* Easter egg: all exported DEMs are centered around Davis, CA: */
	static const double gridCenter[2]={609959.0, 4268028.0};
	double west=gridCenter[0]-double(factory->gridSize[0]-1)*double(factory->cellSize[0])*gs*0.5;
	double east=gridCenter[0]+double(factory->gridSize[0]-1)*double(factory->cellSize[0])*gs*0.5;
	double north=gridCenter[1]+double(factory->gridSize[1]-1)*double(factory->cellSize[1])*gs*0.5;
	double south=gridCenter[1]-double(factory->gridSize[1]-1)*double(factory->cellSize[1])*gs*0.5;
	
	/* Go around the polygon in clockwise order, starting in south-west corner: */
	printFloat8(demFile,west);
	printFloat8(demFile,south);
	printFloat8(demFile,west);
	printFloat8(demFile,north);
	printFloat8(demFile,east);
	printFloat8(demFile,north);
	printFloat8(demFile,east);
	printFloat8(demFile,south);
	
	/* Calculate and write the grid's elevation range: */
	GLfloat elevMin,elevMax;
	elevMin=elevMax=bathymetryBuffer[0];
	const GLfloat* bbPtr=bathymetryBuffer+1;
	for(GLsizei count=factory->gridSize[1]*factory->gridSize[0]-1;count>0;--count,++bbPtr)
		{
		if(elevMin>*bbPtr)
			elevMin=*bbPtr;
		if(elevMax<*bbPtr)
			elevMax=*bbPtr;
		}
	
	// DEBUGGING
	std::cout<<elevMin<<", "<<elevMax<<std::endl;
	
	elevMin*=gs;
	elevMax*=gs;
	printFloat8(demFile,elevMin);
	printFloat8(demFile,elevMax);
	
	/* Calculate the elevation quantization offset and scale: */
	double elevationBase=0.0; // double(elevMin+elevMax)*0.5;
	double zScale=1000.0; // Quantize to millimeters by default
	double elevRange=Math::max(Math::abs(elevMax-elevationBase),Math::abs(elevMin-elevationBase));
	if(elevRange!=0.0)
		{
		/* Calculate a power-of-ten scale factor to scale the actual terrain range to -9999 to 9999: */
		zScale=Math::pow(10.0,Math::floor(Math::log10(9999.0/elevRange)));
		}
	
	// DEBUGGING
	// std::cout<<elevationBase<<", "<<zScale<<std::endl;
	// std::cout<<(elevMax-elevationBase)*zScale<<", "<<(elevMin-elevationBase)*zScale<<std::endl;
	
	/* Write the grid rotation angle: */
	printFloat8(demFile,0.0);
	
	/* Write the accuracy code: */
	printInt2(demFile,0); // Unknown accuracy
	
	/* Write the grid scales with full accuracy. Per spec, only integer values are supported: */
	printFloat4(demFile,factory->cellSize[0]*gs);
	printFloat4(demFile,factory->cellSize[1]*gs);
	printFloat4(demFile,1.0/zScale);
	
	/* Write the number of rows and columns in the grid: */
	printInt2(demFile,1); // Number of rows specified in each grid profile
	printInt2(demFile,factory->gridSize[0]); // Number of columns
	
	/* Calculate the total size of the file written so far: */
	size_t fileSize=864U;
	
	/* Write all grid columns: */
	for(GLsizei column=0;column<factory->gridSize[0];++column)
		{
		/* Pad the current file size to a multiple of 1024: */
		size_t paddedSize=(fileSize+1023U)&~size_t(1023U);
		for(;fileSize<paddedSize;++fileSize)
			demFile<<' ';
		
		/* Write the profile header: */
		printInt2(demFile,1); // 1-based starting row index of this profile
		printInt2(demFile,column+1); // 1-based column index of this profile
		printInt2(demFile,factory->gridSize[1]); // Number of rows in profile
		printInt2(demFile,1); // Number of columns in profile
		printFloat8(demFile,west+double(column)*double(factory->cellSize[0])*gs); // Easting of first elevation posting in column
		printFloat8(demFile,south); // Northing of first elevation posting in column
		printFloat8(demFile,elevationBase); // Local datum elevation
		
		/* Calculate and write the profile's elevation range: */
		const GLfloat* pPtr=bathymetryBuffer+column;
		GLfloat elevMin,elevMax;
		elevMin=elevMax=*pPtr;
		pPtr+=factory->gridSize[0];
		for(GLsizei count=factory->gridSize[1]-1;count>0;--count,pPtr+=factory->gridSize[0])
			{
			if(elevMin>*pPtr)
				elevMin=*pPtr;
			if(elevMax<*pPtr)
				elevMax=*pPtr;
			}
		printFloat8(demFile,elevMin*gs);
		printFloat8(demFile,elevMax*gs);
		
		/* Update the file size: */
		fileSize+=6*4+24*5;
		
		/* Quantize and write the profile's elevation postings: */
		pPtr=bathymetryBuffer+column;
		for(GLsizei count=factory->gridSize[1];count>0;--count,pPtr+=factory->gridSize[0])
			{
			/* Check if there is enough space left in the current 1024-character record: */
			size_t paddedSize=(fileSize+1023U)&~size_t(1023U);
			if(paddedSize-fileSize<10U) // Last four characters of each record need to be blank
				{
				/* Pad the record: */
				for(;fileSize<paddedSize;++fileSize)
					demFile<<' ';
				}
			
			/* Quantize and write the posting: */
			double scaled=(double(*pPtr)*gs-elevationBase)*zScale;
			printInt2(demFile,int(Math::floor(scaled+0.5)));
			fileSize+=6;
			}
		}
	
	/* Pad the current file size to a multiple of 1024: */
	size_t paddedSize=(fileSize+1023U)&~size_t(1023U);
	for(;fileSize<paddedSize;++fileSize)
		demFile<<' ';
	
	/* Write a dummy "C" record: */
	for(int i=0;i<10;++i)
		printInt2(demFile,0);
	fileSize+=6*10;
	
	/* Pad the total file size to a multiple of 1024: */
	paddedSize=(fileSize+1023U)&~size_t(1023U);
	for(;fileSize<paddedSize;++fileSize)
		demFile<<' ';
	}

void BathymetrySaverTool::postUpdate(void) const
	{
	/* Connect to the HTTP server: */
	Comm::NetPipePtr pipe=new Comm::TCPPipe(configuration.postUpdateHostName.c_str(),configuration.postUpdatePort);
	
	/* Assemble the PUT request: */
	std::string request;
	request.append("PUT");
	request.push_back(' ');
	request.push_back('/');
	request.append(configuration.postUpdatePage.c_str());
	request.push_back(' ');
	request.append("HTTP/1.1\r\n");
	
	request.append("Host: ");
	request.append(configuration.postUpdateHostName.c_str());
	request.push_back(':');
	char portString[6];
	request.append(Misc::print(configuration.postUpdatePort,portString+5));
	request.append("\r\n");
	
	request.append("Accept: */*\r\n");
	
	request.append("Content-Length: ");
	char contentLengthString[6];
	request.append(Misc::print(configuration.postUpdateMessage.size(),contentLengthString+5));
	request.append("\r\n");
	
	request.append("Content-Type: application/x-www-form-urlencoded\r\n");
	
	/* Finish the request header: */
	request.append("\r\n");
	
	/* Assemble the PUT request content: */
	request.append(configuration.postUpdateMessage);
	
	/* Send the PUT request: */
	pipe->writeRaw(request.data(),request.size());
	pipe->flush();
	
	/* Parse the reply header: */
	bool replyChunked=false;
	bool replySized=false;
	size_t replySize=0;
	{
	/* Attach a value source to the pipe to parse the server's reply: */
	IO::ValueSource reply(pipe);
	reply.setPunctuation("()<>@,;:\\/[]?={}\r");
	reply.setQuotes("\"");
	reply.skipWs();
	
	/* Read the status line: */
	if(!reply.isLiteral("HTTP")||!reply.isLiteral('/'))
		Misc::throwStdErr("Not an HTTP reply!");
	reply.skipString();
	unsigned int statusCode=reply.readUnsignedInteger();
	if(statusCode!=200)
		Misc::throwStdErr("HTTP error %d: %s",statusCode,reply.readLine().c_str());
	reply.readLine();
	reply.skipWs();
	
	/* Parse reply options until the first empty line: */
	while(!reply.eof()&&reply.peekc()!='\r')
		{
		/* Read the option tag: */
		std::string option=reply.readString();
		if(reply.isLiteral(':'))
			{
			/* Handle the option value: */
			if(option=="Transfer-Encoding")
				{
				/* Parse the comma-separated list of transfer encodings: */
				while(true)
					{
					std::string coding=reply.readString();
					if(coding=="chunked")
						replyChunked=true;
					else
						{
						/* Skip the transfer extension: */
						while(reply.isLiteral(';'))
							{
							reply.skipString();
							if(!reply.isLiteral('='))
								Misc::throwStdErr("Malformed HTTP reply header");
							reply.skipString();
							}
						}
					if(reply.eof()||reply.peekc()!=',')
						break;
					while(!reply.eof()&&reply.peekc()==',')
						reply.readChar();
					}
				}
			else if(option=="Content-Length")
				{
				replySized=true;
				replySize=reply.readUnsignedInteger();
				}
			}
		
		/* Skip the rest of the line: */
		reply.skipLine();
		reply.skipWs();
		}
	
	/* Read the CR/LF pair: */
	if(reply.getChar()!='\r'||reply.getChar()!='\n')
		Misc::throwStdErr("Malformed HTTP reply header");
	}
	
	/* Print the reply entity: */
	if(replyChunked)
		{
		// std::cout<<"Chunked reply body!"<<std::endl<<std::endl;
		
		/* Read all chunks until the end chunk: */
		while(true)
			{
			/* Read the next chunk size: */
			size_t chunkSize=0;
			int digit;
			while(true)
				{
				digit=pipe->getChar();
				if(digit>='0'&&digit<='9')
					chunkSize=(chunkSize<<4)+(digit-'0');
				else if(digit>='a'&&digit<='f')
					chunkSize=(chunkSize<<4)+(digit-'a'+10);
				else if(digit>='A'&&digit<='F')
					chunkSize=(chunkSize<<4)+(digit-'A'+10);
				else
					break;
				}
			while(digit!='\r')
				digit=pipe->getChar();
			if(pipe->getChar()!='\n')
				Misc::throwStdErr("Malformed HTTP chunk header");
			
			if(chunkSize==0)
				break;
			
			/* Read the chunk: */
			char buffer[256];
			while(chunkSize>0)
				{
				size_t readSize=chunkSize;
				if(readSize>sizeof(buffer))
					readSize=sizeof(buffer);
				pipe->read(buffer,readSize);
				// buffer[readSize]='\0';
				// std::cout<<buffer;
				chunkSize-=readSize;
				}
			
			/* Read the chunk footer: */
			if(pipe->getChar()!='\r'||pipe->getChar()!='\n')
				Misc::throwStdErr("Malformed HTTP chunk footer");
			}
		
		/* Skip the body trailer: */
		while(pipe->getChar()!='\r')
			{
			/* Skip the line: */
			while(pipe->getChar()!='\r')
				;
			if(pipe->getChar()!='\n')
				Misc::throwStdErr("Malformed HTTP body trailer");
			}
		if(pipe->getChar()!='\n')
			Misc::throwStdErr("Malformed HTTP body trailer");
		}
	else if(replySized)
		{
		/* Read the fixed reply size: */
		char buffer[256];
		while(replySize>0)
			{
			size_t readSize=replySize;
			if(readSize>sizeof(buffer))
				readSize=sizeof(buffer);
			pipe->read(buffer,readSize);
			// buffer[readSize]='\0';
			// std::cout<<buffer;
			replySize-=readSize;
			}
		}
	else
		{
		/* Read until end-of-file: */
		char buffer[256];
		while(!pipe->eof())
			{
			pipe->readUpTo(buffer,sizeof(buffer));
			// buffer[bufSize]='\0';
			// std::cout<<buffer;
			}
		}
	// std::cout<<std::endl;
	}

BathymetrySaverToolFactory* BathymetrySaverTool::initClass(WaterTable2* sWaterTable,Vrui::ToolManager& toolManager)
	{
	/* Create the tool factory: */
	factory=new BathymetrySaverToolFactory(sWaterTable,toolManager);
	
	/* Register and return the class: */
	toolManager.addClass(factory,Vrui::ToolManager::defaultToolFactoryDestructor);
	return factory;
	}

BathymetrySaverTool::BathymetrySaverTool(const Vrui::ToolFactory* factory,const Vrui::ToolInputAssignment& inputAssignment)
	:Vrui::Tool(factory,inputAssignment),
	 configuration(BathymetrySaverTool::factory->configuration),
	 bathymetryBuffer(new GLfloat[BathymetrySaverTool::factory->gridSize[1]*BathymetrySaverTool::factory->gridSize[0]]),
	 requestPending(false)
	{
	}

BathymetrySaverTool::~BathymetrySaverTool(void)
	{
	delete[] bathymetryBuffer;
	}

void BathymetrySaverTool::configure(const Misc::ConfigurationFileSection& configFileSection)
	{
	/* Override private configuration data from given configuration file section: */
	configuration.read(configFileSection);
	}

void BathymetrySaverTool::storeState(Misc::ConfigurationFileSection& configFileSection) const
	{
	/* Write private configuration data to given configuration file section: */
	configuration.write(configFileSection);
	}

const Vrui::ToolFactory* BathymetrySaverTool::getFactory(void) const
	{
	return factory;
	}

void BathymetrySaverTool::buttonCallback(int buttonSlotIndex,Vrui::InputDevice::ButtonCallbackData* cbData)
	{
	if(cbData->newButtonState)
		{
		/* Request a bathymetry grid from the water table: */
		requestPending=factory->waterTable->requestBathymetry(bathymetryBuffer);
		}
	}

void BathymetrySaverTool::frame(void)
	{
	if(requestPending&&factory->waterTable->haveBathymetry())
		{
		try
			{
			/* Export the bathymetry grid: */
			writeDEMFile();
			
			if(configuration.postUpdate)
				{
				/* Send an update message to the configured web server: */
				postUpdate();
				}
			}
		catch(std::runtime_error err)
			{
			Misc::formattedUserError("Save Bathymetry: Unable to save bathymetry due to exception \"%s\"",err.what());
			}
		
		requestPending=false;
		}
	}
