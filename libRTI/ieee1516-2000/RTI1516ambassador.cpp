#include "RTI1516ambassador.h"
#include <RTI/RangeBounds.h>

#ifndef _WIN32
#include <cstdlib>
#include <cstring>
#endif

#include "PrettyDebug.hh"

#include "M_Classes.hh"
#include "RTI1516HandleFactory.h"
#include "RTI1516fedTime.h"

#include <algorithm>

namespace {

static PrettyDebug D("LIBRTI", __FILE__);
static PrettyDebug G("GENDOC",__FILE__) ;

}

namespace rti1516
{
	/* Deletor Object */
	template <class T>
	struct Deletor {
		void operator() (T* e) {delete e;};
	};

	/* Helper functions */
	template<typename T>
	void
	RTI1516ambassador::assignPHVMAndExecuteService(const rti1516::ParameterHandleValueMap &PHVM, T &req, T &rep) {

		req.setParametersSize(PHVM.size());
		req.setValuesSize(PHVM.size());
		uint32_t i = 0;
		for ( rti1516::ParameterHandleValueMap::const_iterator it = PHVM.begin(); it != PHVM.end(); it++, ++i)
		{
		    req.setParameters(ParameterHandleFriend::toCertiHandle(it->first),i);
		    certi::ParameterValue_t paramValue;
		    paramValue.resize(it->second.size());
		    memcpy(&(paramValue[0]), it->second.data(), it->second.size());
		    req.setValues(paramValue, i);
		}
		privateRefs->executeService(&req, &rep);
	}

	template<typename T>
	void
	RTI1516ambassador::assignAHVMAndExecuteService(const rti1516::AttributeHandleValueMap &AHVM, T &req, T &rep) {

		req.setAttributesSize(AHVM.size());
		req.setValuesSize(AHVM.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleValueMap::const_iterator it = AHVM.begin(); it != AHVM.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(it->first),i);
 			certi::AttributeValue_t attrValue;
 			attrValue.resize(it->second.size());
 			memcpy(&(attrValue[0]), it->second.data(), it->second.size());
 			req.setValues(attrValue, i);  
		}
		privateRefs->executeService(&req, &rep);
	}

	template<typename T>
	void
	RTI1516ambassador::assignAHSAndExecuteService(const rti1516::AttributeHandleSet &AHS, T &req, T &rep) {
		req.setAttributesSize(AHS.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = AHS.begin(); it != AHS.end(); it++, ++i)
		{
			certi::AttributeHandle certiHandle = AttributeHandleFriend::toCertiHandle(*it);
			req.setAttributes(certiHandle,i);
		}
		privateRefs->executeService(&req, &rep);
	}

	std::string varLengthDataAsString(VariableLengthData varLengthData) {
		std::string retVal( (char*)varLengthData.data(), varLengthData.size() );
		return retVal;
	}

	certi::TransportType toCertiTransportationType(rti1516::TransportationType theType) {
		return (theType == rti1516::RELIABLE) ? certi::RELIABLE : certi::BEST_EFFORT;
	}
	rti1516::TransportationType toRTI1516TransportationType(certi::TransportType theType) {
		return (theType == certi::RELIABLE) ? rti1516::RELIABLE : rti1516::BEST_EFFORT;
	}
	certi::OrderType toCertiOrderType(rti1516::OrderType theType) {
		return (theType == rti1516::RECEIVE) ? certi::RECEIVE : certi::TIMESTAMP;
	}
	rti1516::OrderType toRTI1516OrderType(certi::OrderType theType) {
		return (theType == certi::RECEIVE) ? rti1516::RECEIVE : rti1516::TIMESTAMP;
	}
	/* end of Helper functions */

	RTIambassador::RTIambassador() throw()
	{
	}

	RTIambassador::~RTIambassador()
	{
	}


	RTI1516ambassador::RTI1516ambassador() throw()
		: privateRefs(0)
	{
	}

	RTI1516ambassador::~RTI1516ambassador()
	{
		certi::M_Close_Connexion req, rep ;

		G.Out(pdGendoc,"        ====>executeService CLOSE_CONNEXION");
		privateRefs->executeService(&req, &rep);
		// after the response is received, the privateRefs->socketUn must not be used

		delete privateRefs;
	}

		// ----------------------------------------------------------------------------
	//! Generic callback evocation (CERTI extension).
	/*! Blocks up to "minimum" seconds until a callback delivery and then evokes a
	 *  single callback.
	 *  @return true if additional callbacks pending, false otherwise
	 */
	bool RTI1516ambassador::__tick_kernel(bool multiple, TickTime minimum, TickTime maximum)
	throw (rti1516::SpecifiedSaveLabelDoesNotExist,
			rti1516::RTIinternalError)
	{
		M_Tick_Request vers_RTI;
		std::auto_ptr<Message> vers_Fed(NULL);

		// Request callback(s) from the local RTIA
		vers_RTI.setMultiple(multiple);
		vers_RTI.setMinTickTime(minimum);
		vers_RTI.setMaxTickTime(maximum);

		try {
			vers_RTI.send(privateRefs->socketUn,privateRefs->msgBufSend);
		}
		catch (NetworkError &e) {
			std::stringstream msg;
			msg << "NetworkError in tick() while sending TICK_REQUEST: " << e._reason;
			std::wstring message(msg.str().begin(), msg.str().end());
			throw RTIinternalError(message);
		}

		// Read response(s) from the local RTIA until Message::TICK_REQUEST is received.
		while (1) {
			try {
				vers_Fed.reset(M_Factory::receive(privateRefs->socketUn));
			}
			catch (NetworkError &e) {
				std::stringstream msg;
				msg << "NetworkError in tick() while receiving response: " << e._reason;
				std::wstring message(msg.str().begin(), msg.str().end());
				throw RTIinternalError(message);
			}

			// If the type is TICK_REQUEST, the __tick_kernel() has terminated.
			if (vers_Fed->getMessageType() == Message::TICK_REQUEST) {
				if (vers_Fed->getExceptionType() != e_NO_EXCEPTION) {
					// tick() may only throw exceptions defined in the HLA standard
					// the RTIA is responsible for sending 'allowed' exceptions only
					privateRefs->processException(vers_Fed.get());
				}
				return static_cast<M_Tick_Request*>(vers_Fed.get())->getMultiple();
			}

			try {
				// Otherwise, the RTI calls a FederateAmbassador service.
				privateRefs->callFederateAmbassador(vers_Fed.get());
			}
			catch (RTIinternalError&) {
				// RTIA awaits TICK_REQUEST_NEXT, terminate the tick() processing
				privateRefs->sendTickRequestStop();
				// ignore the response and re-throw the original exception
				throw;
			}

			try {
				// Request next callback from the RTIA
				M_Tick_Request_Next tick_next;
				tick_next.send(privateRefs->socketUn, privateRefs->msgBufSend);
			}
			catch (NetworkError &e) {
				std::stringstream msg;
				msg << "NetworkError in tick() while sending TICK_REQUEST_NEXT: " << e._reason;

				std::wstring message(msg.str().begin(), msg.str().end());
				throw RTIinternalError(message);
			}
		} // while(1)
	}

	// 4.2
	void RTI1516ambassador::createFederationExecution
		(std::wstring const & federationExecutionName,
		std::wstring const & fullPathNameToTheFDDfile,
		std::wstring const & logicalTimeImplementationName)
		throw (FederationExecutionAlreadyExists,
		CouldNotOpenFDD,
		ErrorReadingFDD,
		CouldNotCreateLogicalTimeFactory,
		RTIinternalError)
	{ 
		/* TODO */
		certi::M_Create_Federation_Execution req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::createFederationExecution");
		std::string federationExecutionNameAsString(federationExecutionName.begin(), federationExecutionName.end());
		req.setFederationName(federationExecutionNameAsString);
		
		std::string fullPathNameToTheFDDfileAsString(fullPathNameToTheFDDfile.begin(), fullPathNameToTheFDDfile.end());
		req.setFEDid(fullPathNameToTheFDDfileAsString);

		/*#ifdef _WIN32
		if(!stricmp(FED,executionName)) {
		#else
		if(!strcasecmp(FED,exeName)) {
		#endif
		}*/
		G.Out(pdGendoc,"             ====>executeService CREATE_FEDERATION_EXECUTION");

		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit RTI1516ambassador::createFederationExecution");

		// TODO What to do with the 'logicalTimeImplementationName'? Can't find it's use in SISO-STD-004.1-2004
		// Only exists in C++ interface.
		// Ignored for now.
	}

	// 4.3
	void RTI1516ambassador::destroyFederationExecution 
		(std::wstring const & federationExecutionName)
		throw (FederatesCurrentlyJoined,
		FederationExecutionDoesNotExist,
		RTIinternalError)
	{ 
		M_Destroy_Federation_Execution req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::destroyFederationExecution");

		std::string federationExecutionNameAsString(federationExecutionName.begin(), federationExecutionName.end());
		req.setFederationName(federationExecutionNameAsString);

		G.Out(pdGendoc,"        ====>executeService DESTROY_FEDERATION_EXECUTION");

		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit RTI1516ambassador::destroyFederationExecution");
	}

	// 4.4
	FederateHandle RTI1516ambassador::joinFederationExecution 
		(std::wstring const & federateType,
		std::wstring const & federationExecutionName,
		FederateAmbassador & federateAmbassador)
		throw (FederateAlreadyExecutionMember,
		FederationExecutionDoesNotExist,
		SaveInProgress,
		RestoreInProgress,
		CouldNotCreateLogicalTimeFactory,
		RTIinternalError)
	{ 
		M_Join_Federation_Execution req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::joinFederationExecution");

		if ( &federateType == NULL || federateType.length() <= 0 ) {
			throw RTIinternalError(L"Incorrect or empty federate name");
		}
		std::string federateTypeAsString(federateType.begin(), federateType.end());

		if ( &federationExecutionName == NULL || federationExecutionName.length() <= 0 )
			throw RTIinternalError(L"Incorrect or empty federation name");	
		std::string federationExecutionNameAsString(federationExecutionName.begin(), federationExecutionName.end()); 

		privateRefs->fed_amb = &federateAmbassador ;

		req.setFederateName(federateTypeAsString);
		req.setFederationName(federationExecutionNameAsString);
		G.Out(pdGendoc,"        ====>executeService JOIN_FEDERATION_EXECUTION");
		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::joinFederationExecution");
		PrettyDebug::setFederateName( "LibRTI::"+std::string(federateTypeAsString));
		
		certi::FederateHandle certiFederateHandle = rep.getFederate();
		rti1516::FederateHandle rti1516FederateHandle = rti1516::FederateHandleFriend::createRTI1516Handle(certiFederateHandle);

		return rti1516FederateHandle;
	}

	// 4.5
	void RTI1516ambassador::resignFederationExecution
		(ResignAction resignAction)
		throw (OwnershipAcquisitionPending,
		FederateOwnsAttributes,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Resign_Federation_Execution req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::resignFederationExecution");
		//req.setResignAction(static_cast<certi::ResignAction>(resignAction));
		req.setResignAction(certi::DELETE_OBJECTS_AND_RELEASE_ATTRIBUTES);
		G.Out(pdGendoc,"        ====>executeService RESIGN_FEDERATION_EXECUTION");
		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit RTI1516ambassador::resignFederationExecution");
	}

	// 4.6
	void RTI1516ambassador::registerFederationSynchronizationPoint
		(std::wstring const & label,
		VariableLengthData const & theUserSuppliedTag)
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Register_Federation_Synchronization_Point req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::registerFederationSynchronizationPoint for all federates");
		std::string labelString(label.begin(), label.end());
		req.setLabel(labelString);
		// no federate set
		req.setFederateSetSize(0);
		if ( &theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL )
		{
			throw RTIinternalError (L"Calling registerFederationSynchronizationPoint with Tag NULL");
		}
		req.setTag(varLengthDataAsString(theUserSuppliedTag));
		G.Out(pdGendoc,"        ====>executeService REGISTER_FEDERATION_SYNCHRONIZATION_POINT");
		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit RTI1516ambassador::registerFederationSynchronizationPoint for all federates");
	}

	void RTI1516ambassador::registerFederationSynchronizationPoint
		(std::wstring const & label,
		VariableLengthData const & theUserSuppliedTag,
		FederateHandleSet const & syncSet)
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Register_Federation_Synchronization_Point req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::registerFederationSynchronizationPoint for some federates");

		std::string labelString(label.begin(), label.end());
		req.setLabel(labelString);
		if ( &theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL )
		{
			throw RTIinternalError (L"Calling registerFederationSynchronizationPoint with Tag NULL");
		}
		req.setTag(varLengthDataAsString(theUserSuppliedTag));
		// Federate set exists but if size=0 (set empty)
		// We have to send the size even if federate set size is 0
		// (HLA 1.3 compliance to inform ALL federates)

		req.setFederateSetSize(syncSet.size());

		uint32_t i = 0;
		for ( rti1516::FederateHandleSet::const_iterator it = syncSet.begin(); it != syncSet.end(); it++, ++i)
		{
			req.setFederateSet(FederateHandleFriend::toCertiHandle(*it),i); 
		}

		G.Out(pdGendoc,"        ====>executeService REGISTER_FEDERATION_SYNCHRONIZATION_POINT");
		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit RTI1516ambassador::registerFederationSynchronizationPoint for some federates");
	}

	// 4.9
	void RTI1516ambassador::synchronizationPointAchieved
		(std::wstring const & label)
		throw (SynchronizationPointLabelNotAnnounced,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Synchronization_Point_Achieved req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::synchronizationPointAchieved");

		std::string labelString(label.begin(), label.end());
		req.setLabel(labelString);

		G.Out(pdGendoc,"        ====>executeService SYNCHRONIZATION_POINT_ACHIEVED");
		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit  RTI1516ambassador::synchronizationPointAchieved"); 
	}

	// 4.11
	void RTI1516ambassador::requestFederationSave
		(std::wstring const & label)
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Request_Federation_Save req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::requestFederationSave without time");

		std::string labelString(label.begin(), label.end());
		req.setLabel(labelString);
		G.Out(pdGendoc,"      ====>executeService REQUEST_FEDERATION_SAVE");

		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::requestFederationSave without time");
	}

	void RTI1516ambassador::requestFederationSave
		(std::wstring const & label,
		LogicalTime const & theTime)
		throw (LogicalTimeAlreadyPassed,
		InvalidLogicalTime,
		FederateUnableToUseTime,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Request_Federation_Save req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::requestFederationSave with time");

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		std::string labelString(label.begin(), label.end());
		req.setLabel(labelString);

		G.Out(pdGendoc,"        ====>executeService REQUEST_FEDERATION_SAVE");
		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit RTI1516ambassador::requestFederationSave with time");
	}

	// 4.13
	void RTI1516ambassador::federateSaveBegun ()
		throw (SaveNotInitiated,
		FederateNotExecutionMember,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Federate_Save_Begun req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::federateSaveBegun");

		G.Out(pdGendoc,"      ====>executeService FEDERATE_SAVE_BEGUN");
		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit  RTI1516ambassador::federateSaveBegun");
	}

	// 4.14
	void RTI1516ambassador::federateSaveComplete ()
		throw (FederateHasNotBegunSave,
		FederateNotExecutionMember,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Federate_Save_Complete req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::federateSaveComplete");
		G.Out(pdGendoc,"      ====>executeService FEDERATE_SAVE_COMPLETE");
		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::federateSaveComplete");
	}

	void RTI1516ambassador::federateSaveNotComplete()
		throw (FederateHasNotBegunSave,
		FederateNotExecutionMember,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Federate_Save_Not_Complete req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::federateSaveNotComplete");
		G.Out(pdGendoc,"      ====>executeService FEDERATE_SAVE_NOT_COMPLETE");
		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit  RTI1516ambassador::federateSaveNotComplete");
	}

	// 4.16
	void RTI1516ambassador::queryFederationSaveStatus ()
		throw (FederateNotExecutionMember,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 4.18
	void RTI1516ambassador::requestFederationRestore
		(std::wstring const & label)
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Request_Federation_Restore req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::requestFederationRestore");
		std::string labelString(label.begin(), label.end());
		req.setLabel(labelString);
		G.Out(pdGendoc,"      ====>executeService REQUEST_FEDERATION_RESTORE");
		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::requestFederationRestore");
	}

	// 4.22
	void RTI1516ambassador::federateRestoreComplete ()
		throw (RestoreNotRequested,
		FederateNotExecutionMember,
		SaveInProgress,
		RTIinternalError)
	{ 
		M_Federate_Restore_Complete req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::federateRestoreComplete");

		G.Out(pdGendoc,"      ====>executeService FEDERATE_RESTORE_COMPLETE");
		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::federateRestoreComplete");
	}

	void RTI1516ambassador::federateRestoreNotComplete ()
		throw (RestoreNotRequested,
		FederateNotExecutionMember,
		SaveInProgress,
		RTIinternalError)
	{ 
		M_Federate_Restore_Not_Complete req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::federateRestoreNotComplete");
		G.Out(pdGendoc,"      ====>executeService FEDERATE_RESTORE_NOT_COMPLETE");

		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::federateRestoreNotComplete");
	}

	// 4.24
	void RTI1516ambassador::queryFederationRestoreStatus ()
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	/////////////////////////////////////
	// Declaration Management Services //
	/////////////////////////////////////

	// 5.2
	void RTI1516ambassador::publishObjectClassAttributes
		(ObjectClassHandle theClass,
		rti1516::AttributeHandleSet const & attributeList)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Publish_Object_Class req, rep ;
		G.Out(pdGendoc,"enter RTI1516ambassador::publishObjectClass");

		const certi::ObjectClassHandle objectClassHandle =  ObjectClassHandleFriend::toCertiHandle(theClass);
		req.setObjectClass(objectClassHandle);

		req.setAttributesSize(attributeList.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = attributeList.begin(); it != attributeList.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i);
		}
		G.Out(pdGendoc,"      ====>executeService PUBLISH_OBJECT_CLASS");
		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::publishObjectClass");
	}

	// 5.3
	void RTI1516ambassador::unpublishObjectClass
		(ObjectClassHandle theClass)
		throw (ObjectClassNotDefined,
		OwnershipAcquisitionPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Unpublish_Object_Class req, rep ;
		G.Out(pdGendoc,"enter RTI1516ambassador::unpublishObjectClass");

		const certi::ObjectClassHandle objectClassHandle = ObjectClassHandleFriend::toCertiHandle(theClass);
		req.setObjectClass(objectClassHandle);
		G.Out(pdGendoc,"      ====>executeService UNPUBLISH_OBJECT_CLASS");
		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::unpublishObjectClass");
	}

	void RTI1516ambassador::unpublishObjectClassAttributes
		(ObjectClassHandle theClass,
		AttributeHandleSet const & attributeList)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		OwnershipAcquisitionPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 5.4
	void RTI1516ambassador::publishInteractionClass
		(InteractionClassHandle theInteraction)
		throw (InteractionClassNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Publish_Interaction_Class req, rep ;
		const certi::InteractionClassHandle classHandle = InteractionClassHandleFriend::toCertiHandle(theInteraction);
		req.setInteractionClass(classHandle);
		G.Out(pdGendoc,"      ====>executeService PUBLISH_INTERACTION_CLASS");
		privateRefs->executeService(&req, &rep);
	}

	// 5.5
	void RTI1516ambassador::unpublishInteractionClass
		(InteractionClassHandle theInteraction)
		throw (InteractionClassNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Unpublish_Interaction_Class req, rep ;
		const certi::InteractionClassHandle classHandle = InteractionClassHandleFriend::toCertiHandle(theInteraction);
		req.setInteractionClass(classHandle);

		privateRefs->executeService(&req, &rep);
	}

	// 5.6
	void RTI1516ambassador::subscribeObjectClassAttributes
		(ObjectClassHandle theClass,
		AttributeHandleSet const & attributeList,
		bool active)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Subscribe_Object_Class_Attributes req, rep ;
		G.Out(pdGendoc,"enter RTI1516ambassador::subscribeObjectClassAttributes");

		const certi::ObjectClassHandle objectClassHandle = ObjectClassHandleFriend::toCertiHandle(theClass);
		req.setObjectClass(objectClassHandle);

		req.setAttributesSize(attributeList.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = attributeList.begin(); it != attributeList.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i);
		}
		req.setActive(active);

		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::subscribeObjectClassAttributes");
	}

	// 5.7
	void RTI1516ambassador::unsubscribeObjectClass
		(ObjectClassHandle theClass)
		throw (ObjectClassNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Unsubscribe_Object_Class req, rep ;

		const certi::ObjectClassHandle objectClassHandle = ObjectClassHandleFriend::toCertiHandle(theClass);
		req.setObjectClass(objectClassHandle);

		privateRefs->executeService(&req, &rep); 
	}

	void RTI1516ambassador::unsubscribeObjectClassAttributes
		(ObjectClassHandle theClass,
		AttributeHandleSet const & attributeList)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 5.8
	void RTI1516ambassador::subscribeInteractionClass
		(InteractionClassHandle theClass,
		bool active)
		throw (InteractionClassNotDefined,
		FederateServiceInvocationsAreBeingReportedViaMOM,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Subscribe_Interaction_Class req, rep ;
		const certi::InteractionClassHandle classHandle = InteractionClassHandleFriend::toCertiHandle(theClass);
		req.setInteractionClass(classHandle);

		privateRefs->executeService(&req, &rep);
	}

	// 5.9
	void RTI1516ambassador::unsubscribeInteractionClass
		(InteractionClassHandle theClass)
		throw (InteractionClassNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Unsubscribe_Interaction_Class req, rep ;

		const certi::InteractionClassHandle classHandle = InteractionClassHandleFriend::toCertiHandle(theClass);
		req.setInteractionClass(classHandle);

		privateRefs->executeService(&req, &rep);
	}

	////////////////////////////////
	// Object Management Services //
	////////////////////////////////

	// 6.2
	void RTI1516ambassador::reserveObjectInstanceName
		(std::wstring const & theObjectInstanceName)
		throw (IllegalName,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Reserve_Object_Instance_Name req, rep;

		std::string objInstanceName(theObjectInstanceName.begin(), theObjectInstanceName.end());
		req.setObjectName(objInstanceName);
		privateRefs->executeService(&req, &rep);
	}

	// 6.4
	ObjectInstanceHandle RTI1516ambassador::registerObjectInstance
		(ObjectClassHandle theClass)
		throw (ObjectClassNotDefined,
		ObjectClassNotPublished,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Register_Object_Instance req, rep ;

		req.setObjectClass(rti1516::ObjectClassHandleFriend::toCertiHandle(theClass));
		privateRefs->executeService(&req, &rep);
		return rti1516::ObjectInstanceHandleFriend::createRTI1516Handle(rep.getObject());
	}

	ObjectInstanceHandle RTI1516ambassador::registerObjectInstance
		(ObjectClassHandle theClass,
		std::wstring const & theObjectInstanceName)
		throw (ObjectClassNotDefined,
		ObjectClassNotPublished,
		ObjectInstanceNameNotReserved,
		ObjectInstanceNameInUse,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Register_Object_Instance req, rep ;

		std::string nameString(theObjectInstanceName.begin(), theObjectInstanceName.end());
		req.setObjectName(nameString);
		req.setObjectClass(rti1516::ObjectClassHandleFriend::toCertiHandle(theClass));
		privateRefs->executeService(&req, &rep);

		return rti1516::ObjectInstanceHandleFriend::createRTI1516Handle(rep.getObject());
	}

	// 6.6
	void RTI1516ambassador::updateAttributeValues
		(ObjectInstanceHandle theObject,
		AttributeHandleValueMap const & theAttributeValues,
		VariableLengthData const & theUserSuppliedTag)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		G.Out(pdGendoc,"enter RTI1516ambassador::updateAttributeValues without time");
		M_Update_Attribute_Values req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		if ( &theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL)
		{
			throw RTIinternalError(L"Calling updateAttributeValues with Tag NULL");
		}

		req.setTag(varLengthDataAsString(theUserSuppliedTag));

		assignAHVMAndExecuteService(theAttributeValues, req, rep);

		G.Out(pdGendoc,"exit  RTI1516ambassador::updateAttributeValues without time");
	}

	MessageRetractionHandle RTI1516ambassador::updateAttributeValues
		(ObjectInstanceHandle theObject,
		AttributeHandleValueMap const & theAttributeValues,
		VariableLengthData const & theUserSuppliedTag,
		LogicalTime const & theTime)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		InvalidLogicalTime,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		G.Out(pdGendoc,"enter RTI1516ambassador::updateAttributeValues with time");
		M_Update_Attribute_Values req, rep ;
		
		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		
		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		if ( &theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL)
		{
			throw RTIinternalError(L"Calling updateAttributeValues with Tag NULL");
		}

		req.setTag(varLengthDataAsString(theUserSuppliedTag));
		
		assignAHVMAndExecuteService(theAttributeValues, req, rep);

		G.Out(pdGendoc,"return  RTI1516ambassador::updateAttributeValues with time");
		certi::FederateHandle certiHandle = rep.getEventRetraction().getSendingFederate();
		uint64_t serialNum = rep.getEventRetraction().getSN();
		return rti1516::MessageRetractionHandleFriend::createRTI1516Handle(certiHandle, serialNum);
	}

	// 6.8
	void RTI1516ambassador::sendInteraction
		(InteractionClassHandle theInteraction,
		ParameterHandleValueMap const & theParameterValues,
		VariableLengthData const & theUserSuppliedTag)
		throw (InteractionClassNotPublished,
		InteractionClassNotDefined,
		InteractionParameterNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Send_Interaction req, rep ;

		const certi::InteractionClassHandle classHandle = InteractionClassHandleFriend::toCertiHandle(theInteraction);
		req.setInteractionClass(classHandle);

		if (&theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL )
		{
			throw RTIinternalError (L"Calling sendIntercation with Tag NULL") ;
		}

		req.setTag(varLengthDataAsString(theUserSuppliedTag));
		req.setRegion(0);
		
		assignPHVMAndExecuteService(theParameterValues, req, rep);
	}

	MessageRetractionHandle RTI1516ambassador::sendInteraction
		(InteractionClassHandle theInteraction,
		ParameterHandleValueMap const & theParameterValues,
		VariableLengthData const & theUserSuppliedTag,
		LogicalTime const & theTime)
		throw (InteractionClassNotPublished,
		InteractionClassNotDefined,
		InteractionParameterNotDefined,
		InvalidLogicalTime,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Send_Interaction req, rep ;

		const certi::InteractionClassHandle classHandle = InteractionClassHandleFriend::toCertiHandle(theInteraction);
		req.setInteractionClass(classHandle);

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		if (&theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL ) {
			throw RTIinternalError(L"Calling sendInteraction with Tag NULL") ;
		}

		req.setTag(varLengthDataAsString(theUserSuppliedTag));
		req.setRegion(0);
		
		assignPHVMAndExecuteService(theParameterValues, req, rep);

		certi::FederateHandle certiHandle = rep.getEventRetraction().getSendingFederate();
		uint64_t serialNr = rep.getEventRetraction().getSN();
		rti1516::MessageRetractionHandle rti1516handle = MessageRetractionHandleFriend::createRTI1516Handle(certiHandle, serialNr);
		
		return rti1516handle;
	}

	// 6.10
	void RTI1516ambassador::deleteObjectInstance
		(ObjectInstanceHandle theObject,
		VariableLengthData const & theUserSuppliedTag)
		throw (DeletePrivilegeNotHeld,
		ObjectInstanceNotKnown,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Delete_Object_Instance req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		if ( &theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL)
		{
			throw RTIinternalError(L"Calling deleteObjectInstance with Tag NULL") ;
		}

		req.setTag(varLengthDataAsString(theUserSuppliedTag));

		privateRefs->executeService(&req, &rep);
	}

	MessageRetractionHandle RTI1516ambassador::deleteObjectInstance
		(ObjectInstanceHandle theObject,
		VariableLengthData const & theUserSuppliedTag,
		LogicalTime  const & theTime)
		throw (DeletePrivilegeNotHeld,
		ObjectInstanceNotKnown,
		InvalidLogicalTime,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Delete_Object_Instance req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		if ( &theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL)
		{
			throw RTIinternalError(L"Calling deleteObjectInstance with Tag NULL") ;
		}

		req.setTag(varLengthDataAsString(theUserSuppliedTag));

		privateRefs->executeService(&req, &rep);

		certi::FederateHandle certiHandle = rep.getEventRetraction().getSendingFederate();
		uint64_t serialNum = rep.getEventRetraction().getSN();
		return rti1516::MessageRetractionHandleFriend::createRTI1516Handle(certiHandle, serialNum);
	}

	// 6.12
	void RTI1516ambassador::localDeleteObjectInstance
		(ObjectInstanceHandle theObject)
		throw (ObjectInstanceNotKnown,
		FederateOwnsAttributes,
		OwnershipAcquisitionPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		throw RTIinternalError(L"unimplemented service localDeleteObjectInstance");
		M_Local_Delete_Object_Instance req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		privateRefs->executeService(&req, &rep); 
	}

	// 6.13
	void RTI1516ambassador::changeAttributeTransportationType
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes,
		TransportationType theType)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Change_Attribute_Transportation_Type req, rep ;
		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));	
		req.setTransportationType(toCertiTransportationType(theType));
		
		req.setAttributesSize(theAttributes.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = theAttributes.begin(); it != theAttributes.end(); it++, ++i)
		{
			req.setAttributes(rti1516::AttributeHandleFriend::toCertiHandle(*it),i);
		}

		privateRefs->executeService(&req, &rep);
	}

	// 6.14
	void RTI1516ambassador::changeInteractionTransportationType
		(InteractionClassHandle theClass,
		TransportationType theType)
		throw (InteractionClassNotDefined,
		InteractionClassNotPublished,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Change_Interaction_Transportation_Type req, rep ;

		req.setInteractionClass(rti1516::InteractionClassHandleFriend::toCertiHandle(theClass));
		req.setTransportationType(toCertiTransportationType(theType));

		privateRefs->executeService(&req, &rep);
	}

	// 6.17
	void RTI1516ambassador::requestAttributeValueUpdate
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes,
		VariableLengthData const & theUserSuppliedTag)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Request_Object_Attribute_Value_Update req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::requestObjectAttributeValueUpdate");
		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));

		size_t attr_num = theAttributes.size();
		req.setAttributesSize( attr_num );
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = theAttributes.begin(); i < attr_num; ++it, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i);
		}
		req.setTag(varLengthDataAsString(theUserSuppliedTag));

		privateRefs->executeService(&req, &rep);
		G.Out(pdGendoc,"exit  RTI1516ambassador::requestObjectAttributeValueUpdate");
	}

	void RTI1516ambassador::requestAttributeValueUpdate
		(ObjectClassHandle theClass,
		AttributeHandleSet const & theAttributes,
		VariableLengthData const & theUserSuppliedTag)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Request_Class_Attribute_Value_Update req, rep ;
		G.Out(pdGendoc,"enter RTI1516ambassador::requestClassAttributeValueUpdate");
		req.setObjectClass(rti1516::ObjectClassHandleFriend::toCertiHandle(theClass));

		assignAHSAndExecuteService(theAttributes, req, rep);

		G.Out(pdGendoc,"exit RTI1516ambassador::requestClassAttributeValueUpdate");
	}

	///////////////////////////////////
	// Ownership Management Services //
	///////////////////////////////////
	// 7.2
	void RTI1516ambassador::unconditionalAttributeOwnershipDivestiture
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Unconditional_Attribute_Ownership_Divestiture req, rep ;
		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));

		req.setAttributesSize(theAttributes.size());
		uint32_t i=0;
		for ( rti1516::AttributeHandleSet::const_iterator it = theAttributes.begin(); it != theAttributes.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i); 
		}

		privateRefs->executeService(&req, &rep);
	}

	// 7.3
	void RTI1516ambassador::negotiatedAttributeOwnershipDivestiture
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes,
		VariableLengthData const & theUserSuppliedTag)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		AttributeAlreadyBeingDivested,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Negotiated_Attribute_Ownership_Divestiture req, rep ;
		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		if (&theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL) {
			throw RTIinternalError (L"Calling negotiatedAttributeOwnershipDivestiture with Tag NULL") ;
		}
		req.setTag(rti1516::varLengthDataAsString(theUserSuppliedTag));
		
		req.setAttributesSize(theAttributes.size());
		uint32_t i=0;
		for ( rti1516::AttributeHandleSet::const_iterator it = theAttributes.begin(); it != theAttributes.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i); 
		}

		privateRefs->executeService(&req, &rep);
	}

	// 7.6
	void RTI1516ambassador::confirmDivestiture
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & confirmedAttributes,
		VariableLengthData const & theUserSuppliedTag)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		AttributeDivestitureWasNotRequested,
		NoAcquisitionPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 7.8
	void RTI1516ambassador::attributeOwnershipAcquisition
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & desiredAttributes,
		VariableLengthData const & theUserSuppliedTag)
		throw (ObjectInstanceNotKnown,
		ObjectClassNotPublished,
		AttributeNotDefined,
		AttributeNotPublished,
		FederateOwnsAttributes,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Attribute_Ownership_Acquisition req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		if (&theUserSuppliedTag == NULL || theUserSuppliedTag.data() == NULL )
		{
			throw RTIinternalError (L"Calling attributeOwnershipAcquisition with Tag NULL") ;
		}
		req.setTag(rti1516::varLengthDataAsString(theUserSuppliedTag));
		
		req.setAttributesSize(desiredAttributes.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = desiredAttributes.begin(); it != desiredAttributes.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i);
		}
		
		privateRefs->executeService(&req, &rep); 
	}

	// 7.9
	void RTI1516ambassador::attributeOwnershipAcquisitionIfAvailable
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & desiredAttributes)
		throw (ObjectInstanceNotKnown,
		ObjectClassNotPublished,
		AttributeNotDefined,
		AttributeNotPublished,
		FederateOwnsAttributes,
		AttributeAlreadyBeingAcquired,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Attribute_Ownership_Acquisition_If_Available req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		
		req.setAttributesSize(desiredAttributes.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = desiredAttributes.begin(); it != desiredAttributes.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i);
		}

		privateRefs->executeService(&req, &rep);
	}

	// 7.12 (in RTI1.3 this function is called: AttributeOwnershipReleaseResponse)
	void RTI1516ambassador::attributeOwnershipDivestitureIfWanted
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes,
		AttributeHandleSet & theDivestedAttributes) // filled by RTI
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Attribute_Ownership_Release_Response req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));

		assignAHSAndExecuteService(theAttributes, req, rep);

		if (rep.getExceptionType() == e_NO_EXCEPTION) {
			theDivestedAttributes.clear();
			for (uint32_t i=0;i<rep.getAttributesSize();++i) {
				theDivestedAttributes.insert(rti1516::AttributeHandleFriend::createRTI1516Handle(rep.getAttributes()[i]));
			}
		}

	}

	// 7.13
	void RTI1516ambassador::cancelNegotiatedAttributeOwnershipDivestiture
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		AttributeDivestitureWasNotRequested,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Cancel_Negotiated_Attribute_Ownership_Divestiture req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		
		req.setAttributesSize(theAttributes.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = theAttributes.begin(); it != theAttributes.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i);
		}

		privateRefs->executeService(&req, &rep);
	}

	// 7.14
	void RTI1516ambassador::cancelAttributeOwnershipAcquisition
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeAlreadyOwned,
		AttributeAcquisitionWasNotRequested,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Cancel_Attribute_Ownership_Acquisition req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		
		req.setAttributesSize(theAttributes.size());
		uint32_t i = 0;
		for ( rti1516::AttributeHandleSet::const_iterator it = theAttributes.begin(); it != theAttributes.end(); it++, ++i)
		{
			req.setAttributes(AttributeHandleFriend::toCertiHandle(*it),i);
		}

		privateRefs->executeService(&req, &rep); 
	}

	// 7.16
	void RTI1516ambassador::queryAttributeOwnership
		(ObjectInstanceHandle theObject,
		AttributeHandle theAttribute)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Query_Attribute_Ownership req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		req.setAttribute(rti1516::AttributeHandleFriend::toCertiHandle(theAttribute));

		privateRefs->executeService(&req, &rep);
	}

	// 7.18
	bool RTI1516ambassador::isAttributeOwnedByFederate
		(ObjectInstanceHandle theObject,
		AttributeHandle theAttribute)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Is_Attribute_Owned_By_Federate req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		req.setAttribute(rti1516::AttributeHandleFriend::toCertiHandle(theAttribute));

		privateRefs->executeService(&req, &rep);

		return (rep.getTag() == "RTI_TRUE") ? true : false;
	}

	//////////////////////////////
	// Time Management Services //
	//////////////////////////////

	// 8.2
	void RTI1516ambassador::enableTimeRegulation
		(LogicalTimeInterval const & theLookahead)
		throw (TimeRegulationAlreadyEnabled,
		InvalidLookahead,
		InTimeAdvancingState,
		RequestForTimeRegulationPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Enable_Time_Regulation req, rep ;

		//req.setDate(certi_cast<RTIfedTime>()(theFederateTime).getTime());  //JRE: DATE IS NOT USED!
		
		//JRE: is dit wel goed?
		//JvY: TODO Controleren of dit blijft werken met andere tijdsimplementaties
		union ud {
			double   dv;
			uint64_t uv;
		} value;
#ifdef HOST_IS_BIG_ENDIAN
		memcpy(&(value.uv), theLookahead.encode().data(), sizeof(double));
#else
		value.uv = CERTI_DECODE_DOUBLE_FROM_UINT64BE(theLookahead.encode().data());
#endif
		double lookAheadTime = value.dv;
		req.setLookahead(lookAheadTime);
		privateRefs->executeService(&req, &rep);
	}

	// 8.4
	void RTI1516ambassador::disableTimeRegulation ()
		throw (TimeRegulationIsNotEnabled,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Disable_Time_Regulation req, rep ;

		privateRefs->executeService(&req, &rep);
	}

	// 8.5
	void RTI1516ambassador::enableTimeConstrained ()
		throw (TimeConstrainedAlreadyEnabled,
		InTimeAdvancingState,
		RequestForTimeConstrainedPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Enable_Time_Constrained req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 8.7
	void RTI1516ambassador::disableTimeConstrained ()
		throw (TimeConstrainedIsNotEnabled,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Disable_Time_Constrained req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 8.8
	void RTI1516ambassador::timeAdvanceRequest
		(LogicalTime const & theTime)
		throw (InvalidLogicalTime,
		LogicalTimeAlreadyPassed,
		InTimeAdvancingState,
		RequestForTimeRegulationPending,
		RequestForTimeConstrainedPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Time_Advance_Request req, rep ;

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);
		privateRefs->executeService(&req, &rep);
	}

	// 8.9
	void RTI1516ambassador::timeAdvanceRequestAvailable
		(LogicalTime const & theTime)
		throw (InvalidLogicalTime,
		LogicalTimeAlreadyPassed,
		InTimeAdvancingState,
		RequestForTimeRegulationPending,
		RequestForTimeConstrainedPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Time_Advance_Request_Available req, rep ;

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		privateRefs->executeService(&req, &rep); 
	}

	// 8.10
	void RTI1516ambassador::nextMessageRequest
		(LogicalTime const & theTime)
		throw (InvalidLogicalTime,
		LogicalTimeAlreadyPassed,
		InTimeAdvancingState,
		RequestForTimeRegulationPending,
		RequestForTimeConstrainedPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Next_Event_Request req, rep ;

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		privateRefs->executeService(&req, &rep);
	}

	// 8.11
	void RTI1516ambassador::nextMessageRequestAvailable
		(LogicalTime const & theTime)
		throw (InvalidLogicalTime,
		LogicalTimeAlreadyPassed,
		InTimeAdvancingState,
		RequestForTimeRegulationPending,
		RequestForTimeConstrainedPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Next_Event_Request_Available req, rep ;

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		privateRefs->executeService(&req, &rep);
	}

	// 8.12
	void RTI1516ambassador::flushQueueRequest
		(LogicalTime const & theTime)
		throw (InvalidLogicalTime,
		LogicalTimeAlreadyPassed,
		InTimeAdvancingState,
		RequestForTimeRegulationPending,
		RequestForTimeConstrainedPending,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		// JvY: Implementation copied from previous CERTI implementation, including immediate throw.
		throw RTIinternalError(L"Unimplemented Service flushQueueRequest");
		M_Flush_Queue_Request req, rep ;

		certi::FederationTime certiFedTime(certi_cast<RTI1516fedTime>()(theTime).getFedTime());
		req.setDate(certiFedTime);

		privateRefs->executeService(&req, &rep);
	}

	// 8.14
	void RTI1516ambassador::enableAsynchronousDelivery ()
		throw (AsynchronousDeliveryAlreadyEnabled,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		// throw AsynchronousDeliveryAlreadyEnabled("Default value (non HLA)");

		M_Enable_Asynchronous_Delivery req, rep ;

		privateRefs->executeService(&req, &rep);
	}

	// 8.15
	void RTI1516ambassador::disableAsynchronousDelivery ()
		throw (AsynchronousDeliveryAlreadyDisabled,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Disable_Asynchronous_Delivery req, rep ;

		privateRefs->executeService(&req, &rep);
	}

	// 8.16
	bool RTI1516ambassador::queryGALT (LogicalTime & theTime)
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		//TODO JRE: goed testen! Is GALT wel precies het zelfde als LBTS?
		M_Query_Lbts req, rep ;

		privateRefs->executeService(&req, &rep);

		//TODO JRE: goed testen of deze return value wel klopt!
		certi::FederationTime fedTime = rep.getDate();
		if (fedTime.getTime() == 0.0) {
			return false;
		}	

		// JvY: TODO Controleren of dit blijft werken met andere tijdsimplementaties
		certi_cast<RTI1516fedTime>()(theTime) = rep.getDate().getTime();

		return true;
	}

	// 8.17
	void RTI1516ambassador::queryLogicalTime (LogicalTime & theTime)
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Query_Federate_Time req, rep ;

		privateRefs->executeService(&req, &rep);

		// JvY: TODO Controleren of dit blijft werken met andere tijdsimplementaties
		certi_cast<RTI1516fedTime>()(theTime) = rep.getDate().getTime();
	}

	// 8.18
	bool RTI1516ambassador::queryLITS (LogicalTime & theTime)
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		//TODO JRE: goed testen! Is LITS wel precies het zelfde als QueryMinNextEventTime?
		M_Query_Min_Next_Event_Time req, rep ;

		privateRefs->executeService(&req, &rep);

		//TODO JRE: goed testen of deze return value wel klopt!
		certi::FederationTime fedTime = rep.getDate();
		if (fedTime.getTime() == 0.0) {
			return false;
		}
		// JvY: TODO Controleren of dit blijft werken met andere tijdsimplementaties
		certi_cast<RTI1516fedTime>()(theTime) = rep.getDate().getTime();

		return true;
	}

	// 8.19
	void RTI1516ambassador::modifyLookahead
		(LogicalTimeInterval const & theLookahead)
		throw (TimeRegulationIsNotEnabled,
		InvalidLookahead,
		InTimeAdvancingState,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 8.20
	void RTI1516ambassador::queryLookahead (LogicalTimeInterval & interval)
		throw (TimeRegulationIsNotEnabled,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 8.21
	void RTI1516ambassador::retract
		(MessageRetractionHandle theHandle)
		throw (InvalidRetractionHandle,
		TimeRegulationIsNotEnabled,
		MessageCanNoLongerBeRetracted,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		throw RTIinternalError(L"Unimplemented Service retract");
		M_Retract req, rep ;
		
		certi::EventRetraction event = rti1516::MessageRetractionHandleFriend::createEventRetraction(theHandle);
		req.setEventRetraction(event);

		privateRefs->executeService(&req, &rep);
	}

	// 8.23
	void RTI1516ambassador::changeAttributeOrderType
		(ObjectInstanceHandle theObject,
		AttributeHandleSet const & theAttributes,
		OrderType theType)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		AttributeNotOwned,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Change_Attribute_Order_Type req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		req.setOrder(rti1516::toCertiOrderType(theType));

		assignAHSAndExecuteService(theAttributes, req, rep);
	}

	// 8.24
	void RTI1516ambassador::changeInteractionOrderType
		(InteractionClassHandle theClass,
		OrderType theType)
		throw (InteractionClassNotDefined,
		InteractionClassNotPublished,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Change_Interaction_Order_Type req, rep ;

		req.setInteractionClass(rti1516::InteractionClassHandleFriend::toCertiHandle(theClass));
		req.setOrder(rti1516::toCertiOrderType(theType));

		privateRefs->executeService(&req, &rep);
	}

	//////////////////////////////////
	// Data Distribution Management //
	//////////////////////////////////

	// 9.2
	RegionHandle RTI1516ambassador::createRegion
		(DimensionHandleSet const & theDimensions)
		throw (InvalidDimensionHandle,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.3
	void RTI1516ambassador::commitRegionModifications
		(RegionHandleSet const & theRegionHandleSet)
		throw (InvalidRegion,
		RegionNotCreatedByThisFederate,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented"); 
	}

	// 9.4
	void RTI1516ambassador::deleteRegion
		(RegionHandle theRegion)
		throw (InvalidRegion,
		RegionNotCreatedByThisFederate,
		RegionInUseForUpdateOrSubscription,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.5
	ObjectInstanceHandle RTI1516ambassador::registerObjectInstanceWithRegions
		(ObjectClassHandle theClass,
		AttributeHandleSetRegionHandleSetPairVector const &
		theAttributeHandleSetRegionHandleSetPairVector)
		throw (ObjectClassNotDefined,
		ObjectClassNotPublished,
		AttributeNotDefined,
		AttributeNotPublished,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	ObjectInstanceHandle RTI1516ambassador::registerObjectInstanceWithRegions
		(ObjectClassHandle theClass,
		AttributeHandleSetRegionHandleSetPairVector const &
		theAttributeHandleSetRegionHandleSetPairVector,
		std::wstring const & theObjectInstanceName)
		throw (ObjectClassNotDefined,
		ObjectClassNotPublished,
		AttributeNotDefined,
		AttributeNotPublished,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		ObjectInstanceNameNotReserved,
		ObjectInstanceNameInUse,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.6
	void RTI1516ambassador::associateRegionsForUpdates
		(ObjectInstanceHandle theObject,
		AttributeHandleSetRegionHandleSetPairVector const &
		theAttributeHandleSetRegionHandleSetPairVector)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.7
	void RTI1516ambassador::unassociateRegionsForUpdates
		(ObjectInstanceHandle theObject,
		AttributeHandleSetRegionHandleSetPairVector const &
		theAttributeHandleSetRegionHandleSetPairVector)
		throw (ObjectInstanceNotKnown,
		AttributeNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented"); 
	}

	// 9.8
	void RTI1516ambassador::subscribeObjectClassAttributesWithRegions
		(ObjectClassHandle theClass,
		AttributeHandleSetRegionHandleSetPairVector const &
		theAttributeHandleSetRegionHandleSetPairVector,
		bool active)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.9
	void RTI1516ambassador::unsubscribeObjectClassAttributesWithRegions
		(ObjectClassHandle theClass,
		AttributeHandleSetRegionHandleSetPairVector const &
		theAttributeHandleSetRegionHandleSetPairVector)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.10
	void RTI1516ambassador::subscribeInteractionClassWithRegions
		(InteractionClassHandle theClass,
		RegionHandleSet const & theRegionHandleSet,
		bool active)
		throw (InteractionClassNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		FederateServiceInvocationsAreBeingReportedViaMOM,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.11
	void RTI1516ambassador::unsubscribeInteractionClassWithRegions
		(InteractionClassHandle theClass,
		RegionHandleSet const & theRegionHandleSet)
		throw (InteractionClassNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented"); 
	}

	// 9.12
	void RTI1516ambassador::sendInteractionWithRegions
		(InteractionClassHandle theInteraction,
		ParameterHandleValueMap const & theParameterValues,
		RegionHandleSet const & theRegionHandleSet,
		VariableLengthData const & theUserSuppliedTag)
		throw (InteractionClassNotDefined,
		InteractionClassNotPublished,
		InteractionParameterNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	MessageRetractionHandle RTI1516ambassador::sendInteractionWithRegions
		(InteractionClassHandle theInteraction,
		ParameterHandleValueMap const & theParameterValues,
		RegionHandleSet const & theRegionHandleSet,
		VariableLengthData const & theUserSuppliedTag,
		LogicalTime const & theTime)
		throw (InteractionClassNotDefined,
		InteractionClassNotPublished,
		InteractionParameterNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		InvalidLogicalTime,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 9.13
	void RTI1516ambassador::requestAttributeValueUpdateWithRegions
		(ObjectClassHandle theClass,
		AttributeHandleSetRegionHandleSetPairVector const & theSet,
		VariableLengthData const & theUserSuppliedTag)
		throw (ObjectClassNotDefined,
		AttributeNotDefined,
		InvalidRegion,
		RegionNotCreatedByThisFederate,
		InvalidRegionContext,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented"); 
	}

	//////////////////////////
	// RTI Support Services //
	//////////////////////////

	// 10.2
	ObjectClassHandle RTI1516ambassador::getObjectClassHandle
		(std::wstring const & theName)
		throw (NameNotFound,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Object_Class_Handle req, rep ;

		G.Out(pdGendoc,"enter RTI1516ambassador::getObjectClassHandle");

		std::string nameAsString(theName.begin(), theName.end());
		req.setClassName(nameAsString);
		privateRefs->executeService(&req, &rep);

		G.Out(pdGendoc,"exit RTI1516ambassador::getObjectClassHandle");
		rti1516::ObjectClassHandle rti1516Handle = ObjectClassHandleFriend::createRTI1516Handle(rep.getObjectClass());

		return rti1516Handle;
	}

	// 10.3
	std::wstring RTI1516ambassador::getObjectClassName
		(ObjectClassHandle theHandle)
		throw (InvalidObjectClassHandle,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Object_Class_Name req, rep ;

		certi::ObjectClassHandle certiHandle = ObjectClassHandleFriend::toCertiHandle(theHandle);
		req.setObjectClass(certiHandle);
		try {
			privateRefs->executeService(&req, &rep);
		} catch (rti1516::ObjectClassNotDefined &e)
		{
			throw rti1516::InvalidObjectClassHandle(e.what());
		}

		std::string nameString = rep.getClassName();
		std::wstring nameWString(nameString.begin(), nameString.end()); 

		//return hla_strdup(rep.getClassName());
		return nameWString;
	}

	// 10.4
	AttributeHandle RTI1516ambassador::getAttributeHandle
		(ObjectClassHandle whichClass,
		std::wstring const & theAttributeName)
		throw (InvalidObjectClassHandle,
		NameNotFound,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		G.Out(pdGendoc,"enter RTI::RTI1516ambassador::getAttributeHandle");
		M_Get_Attribute_Handle req, rep ;

		std::string nameAsString(theAttributeName.begin(), theAttributeName.end());
		req.setAttributeName(nameAsString);
		req.setObjectClass(rti1516::ObjectClassHandleFriend::toCertiHandle(whichClass));

		try {
			privateRefs->executeService(&req, &rep);
		} catch (rti1516::ObjectClassNotDefined &e)
		{
			if ( ! whichClass.isValid() ) {
				throw rti1516::InvalidObjectClassHandle(e.what());
			} else {
				throw rti1516::NameNotFound(e.what());
			}
		}



		G.Out(pdGendoc,"exit  RTI::RTI1516ambassador::getAttributeHandle");
		return rti1516::AttributeHandleFriend::createRTI1516Handle(rep.getAttribute());
	}

	// 10.5
	std::wstring RTI1516ambassador::getAttributeName
		(ObjectClassHandle whichClass,
		AttributeHandle theHandle)   
		throw (InvalidObjectClassHandle,
		InvalidAttributeHandle,
		AttributeNotDefined,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Attribute_Name req, rep ;

		req.setAttribute(rti1516::AttributeHandleFriend::toCertiHandle(theHandle));
		req.setObjectClass(rti1516::ObjectClassHandleFriend::toCertiHandle(whichClass));
		try {
			privateRefs->executeService(&req, &rep);
		} catch (rti1516::ObjectClassNotDefined &e)
		{
			if ( !whichClass.isValid() )
			{
				throw rti1516::InvalidObjectClassHandle(e.what());
			} else
			{
				throw;
			}
		} catch ( rti1516::AttributeNotDefined &e)
		{
			if (! theHandle.isValid() )
			{
				throw rti1516::InvalidAttributeHandle(e.what());
			} else
			{
				throw;
			}
		}
		
		//return hla_strdup(rep.getAttributeName());

		std::string nameString = rep.getAttributeName();
		std::wstring nameWString(nameString.begin(), nameString.end());

		return nameWString;
	}

	// 10.6
	InteractionClassHandle RTI1516ambassador::getInteractionClassHandle
		(std::wstring const & theName)
		throw (NameNotFound,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Interaction_Class_Handle req, rep ;
		std::string nameString(theName.begin(), theName.end());
		req.setClassName(nameString);

		privateRefs->executeService(&req, &rep);

		return rti1516::InteractionClassHandleFriend::createRTI1516Handle(rep.getInteractionClass());
	}

	// 10.7
	std::wstring RTI1516ambassador::getInteractionClassName
		(InteractionClassHandle theHandle)
		throw (InvalidInteractionClassHandle,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Interaction_Class_Name req, rep ;
		req.setInteractionClass(rti1516::InteractionClassHandleFriend::toCertiHandle(theHandle));
		try {
			privateRefs->executeService(&req, &rep);
		} catch (rti1516::InteractionClassNotDefined &e)
		{
			if ( !theHandle.isValid() )
			{
				throw rti1516::InvalidInteractionClassHandle(e.what());
			} else
			{
				throw;
			}
		}

		//return hla_strdup(rep.getClassName());
		std::string nameString = rep.getClassName();
		std::wstring nameWString(nameString.begin(), nameString.end());

		return nameWString;
	}

	// 10.8
	ParameterHandle RTI1516ambassador::getParameterHandle
		(InteractionClassHandle whichClass,
		std::wstring const & theName)
		throw (InvalidInteractionClassHandle,
		NameNotFound,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Parameter_Handle req, rep ;
		std::string nameString(theName.begin(), theName.end());
		req.setParameterName(nameString);
		req.setInteractionClass(rti1516::InteractionClassHandleFriend::toCertiHandle(whichClass));

		try {
			privateRefs->executeService(&req, &rep);
		} catch (rti1516::InteractionClassNotDefined &e)
		{
			if ( !whichClass.isValid() )
			{
				throw rti1516::InvalidInteractionClassHandle(e.what());
			} else
			{
				throw;
			}
		}

		return rti1516::ParameterHandleFriend::createRTI1516Handle(rep.getParameter());
	}

	// 10.9
	std::wstring RTI1516ambassador::getParameterName
		(InteractionClassHandle whichClass,
		ParameterHandle theHandle)   
		throw (InvalidInteractionClassHandle,
		InvalidParameterHandle,
		InteractionParameterNotDefined,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Parameter_Name req, rep ;

		req.setParameter(rti1516::ParameterHandleFriend::toCertiHandle(theHandle));
		req.setInteractionClass(rti1516::InteractionClassHandleFriend::toCertiHandle(whichClass));

		try {
			privateRefs->executeService(&req, &rep);
		} catch (rti1516::InteractionClassNotDefined &e)
		{
			if ( !whichClass.isValid() )
			{
				throw rti1516::InvalidInteractionClassHandle(e.what());
			} else
			{
				throw;
			}
		}

		//return hla_strdup(rep.getParameterName());
		std::string nameString = rep.getParameterName();
		std::wstring nameWString(nameString.begin(), nameString.end());

		return nameWString;
	}

	// 10.10
	ObjectInstanceHandle RTI1516ambassador::getObjectInstanceHandle
		(std::wstring const & theName)
		throw (ObjectInstanceNotKnown,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Object_Instance_Handle req, rep ;
		std::string nameString(theName.begin(), theName.end());
		req.setObjectInstanceName(nameString);

		privateRefs->executeService(&req, &rep);
		return rti1516::ObjectInstanceHandleFriend::createRTI1516Handle(rep.getObject());
	}

	// 10.11
	std::wstring RTI1516ambassador::getObjectInstanceName
		(ObjectInstanceHandle theHandle)
		throw (ObjectInstanceNotKnown,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Object_Instance_Name req, rep ;
		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theHandle));
		privateRefs->executeService(&req, &rep);

		//return hla_strdup(rep.getObjectInstanceName());
		std::string nameString = rep.getObjectInstanceName();
		std::wstring nameWString(nameString.begin(), nameString.end());

		return nameWString;
	}

	// 10.12
	DimensionHandle RTI1516ambassador::getDimensionHandle
		(std::wstring const & theName)
		throw (NameNotFound,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Dimension_Handle req, rep ;

		std::string nameString(theName.begin(), theName.end());
		req.setDimensionName(nameString);
		//req.setSpace(space);    //SPACE NIET NODIG IN 1516 STANDAARD???
		privateRefs->executeService(&req, &rep);
		return rti1516::DimensionHandleFriend::createRTI1516Handle(rep.getDimension());
	}

	// 10.13
	std::wstring RTI1516ambassador::getDimensionName
		(DimensionHandle theHandle)
		throw (InvalidDimensionHandle,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Dimension_Name req, rep ;

		req.setDimension(rti1516::DimensionHandleFriend::toCertiHandle(theHandle));
		//req.setSpace(space);
		privateRefs->executeService(&req, &rep);
		//return hla_strdup(rep.getDimensionName());
		std::string nameString = rep.getDimensionName();
		std::wstring nameWString(nameString.begin(), nameString.end());

		return nameWString;
	}

	// 10.14
	unsigned long RTI1516ambassador::getDimensionUpperBound
		(DimensionHandle theHandle)   
		throw (InvalidDimensionHandle,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 10.15
	DimensionHandleSet RTI1516ambassador::getAvailableDimensionsForClassAttribute
		(ObjectClassHandle theClass,
		AttributeHandle theHandle)   
		throw (InvalidObjectClassHandle,
		InvalidAttributeHandle,
		AttributeNotDefined,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Attribute_Space_Handle req, rep ;

		req.setAttribute(rti1516::AttributeHandleFriend::toCertiHandle(theHandle));
		req.setObjectClass(rti1516::ObjectClassHandleFriend::toCertiHandle(theClass));
		privateRefs->executeService(&req, &rep);
		
		//JRE TODO: Use space handle to to get the DimensionHandleSet?@!
		//return rep.getSpace();
		DimensionHandleSet invalidSet;
		return invalidSet;
	}

	// 10.16
	ObjectClassHandle RTI1516ambassador::getKnownObjectClassHandle
		(ObjectInstanceHandle theObject)
		throw (ObjectInstanceNotKnown,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Object_Class req, rep ;

		req.setObject(rti1516::ObjectInstanceHandleFriend::toCertiHandle(theObject));
		privateRefs->executeService(&req, &rep);
		return rti1516::ObjectClassHandleFriend::createRTI1516Handle(rep.getObjectClass());
	}

	// 10.17
	DimensionHandleSet RTI1516ambassador::getAvailableDimensionsForInteractionClass
		(InteractionClassHandle theClass)
		throw (InvalidInteractionClassHandle,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Interaction_Space_Handle req, rep ;

		req.setInteractionClass(rti1516::InteractionClassHandleFriend::toCertiHandle(theClass));
		this->privateRefs->executeService(&req, &rep);
		
		//JRE TODO: Use space handle to to get the DimensionHandleSet?@!
		//return rep.getSpace();
		DimensionHandleSet invalidSet;
		return invalidSet;
	}

	// 10.18
	TransportationType RTI1516ambassador::getTransportationType
		(std::wstring const & transportationName)
		throw (InvalidTransportationName,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Transportation_Handle req, rep ;
		std::string nameString(transportationName.begin(), transportationName.end());
		req.setTransportationName(nameString);
		privateRefs->executeService(&req, &rep);

		return rti1516::toRTI1516TransportationType(rep.getTransportation());
	}

	// 10.19
	std::wstring RTI1516ambassador::getTransportationName
		(TransportationType transportationType)
		throw (InvalidTransportationType,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Transportation_Name req, rep ;

		req.setTransportation(rti1516::toCertiTransportationType(transportationType));
		privateRefs->executeService(&req, &rep);

		//return hla_strdup(rep.getTransportationName());
		std::string nameString = rep.getTransportationName();
		std::wstring nameWString(nameString.begin(), nameString.end());

		return nameWString;
	}

	// 10.20
	OrderType RTI1516ambassador::getOrderType
		(std::wstring const & orderName)
		throw (InvalidOrderName,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Ordering_Handle req, rep ;

		std::string nameAsString(orderName.begin(), orderName.end());
		req.setOrderingName(nameAsString);
		privateRefs->executeService(&req, &rep);
		
		return rti1516::toRTI1516OrderType(rep.getOrdering());
	}

	// 10.21
	std::wstring RTI1516ambassador::getOrderName
		(OrderType orderType)
		throw (InvalidOrderType,
		FederateNotExecutionMember,
		RTIinternalError)
	{ 
		M_Get_Ordering_Name req, rep ;

		req.setOrdering(rti1516::toCertiOrderType(orderType));
		privateRefs->executeService(&req, &rep);
		
		//return hla_strdup(rep.getOrderingName());
		std::string nameString = rep.getOrderingName();
		std::wstring nameWString(nameString.begin(), nameString.end());

		return nameWString;
	}

	// 10.22
	/**
	 * Sets the ClassRelevanceAdvisory (CRA) switch to true. The switch 
	 * state is hold on the RTIG side. That's why the message
	 * ENABLE_CLASS_RELEVANCE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the CRA switch is true. This causes a delivery of the
	 * federate service startRegistrationForObjectClass to a publisher 
	 * if there are any new subscribers for the federates published object 
	 * classes. If there are no more subscribers a publisher gets the 
	 * federate service stopRegistrationForObjectClass.
	 *
	 * By disabling the CRA switch the federate is no longer informed by 
	 * subscriptions to its published object classes, i.e. the federate 
	 * services startRegistrationForObjectClass and 
	 * stopRegistrationForObjectClass respectively are not invoked.
	 * @see disableClassRelevanceAdvisorySwitch()
	 */
	void RTI1516ambassador::enableObjectClassRelevanceAdvisorySwitch ()
		throw (ObjectClassRelevanceAdvisorySwitchIsOn,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Enable_Class_Relevance_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 10.23
	/**
	 * Sets the ClassRelevanceAdvisory (CRA) switch to false. The switch
	 * state is hold on the RTIG side. That's why the message
	 * DISABLE_CLASS_RELEVANCE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the CRA switch is true. This causes a delivery of the
	 * federate service startRegistrationForObjectClass to a publisher 
	 * if there are any new subscribers for the federates published object 
	 * classes. If there are no more subscribers a publisher gets the 
	 * federate service stopRegistrationForObjectClass.
	 * @see enableClassRelevanceAdvisorySwitch()
	 *
	 * By disabling the CRA switch the federate is no longer informed by 
	 * subscriptions to its published object classes, i.e. the federate 
	 * services startRegistrationForObjectClass and 
	 * stopRegistrationForObjectClass respectively are not invoked.
	 */
	void RTI1516ambassador::disableObjectClassRelevanceAdvisorySwitch ()
		throw (ObjectClassRelevanceAdvisorySwitchIsOff,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Disable_Class_Relevance_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 10.24
	/**
	 * Sets the AttributeRelevanceAdvisory (ARA) switch to true. The switch 
	 * state is hold on the RTIG side. That's why the message
	 * ENABLE_ATTRIBUTE_RELEVANCE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the ARA switch is false. When enabling the ARA switch
	 * the federate is informed by the federate service 
	 * turnUpdatesOnForObjectInstance of new object instances within remote 
	 * federates actively subscribed to its published attributes. If there
	 * are no active subscribers for a set of instance-attributes the federate
	 * receives the federate service turnUpdatesOffForObjectInstance. 
	 *
	 * By disabling the ARA switch the federate is no longer informed by 
	 * subscriptions to its published attributes, i.e. the federate 
	 * services turnUpdatesOnForObjectInstance and 
	 * turnUpdatesOffForObjectInstance respectively are not invoked.
	 * @see disableAttributeRelevanceAdvisorySwitch()
	 */
	void RTI1516ambassador::enableAttributeRelevanceAdvisorySwitch ()
		throw (AttributeRelevanceAdvisorySwitchIsOn,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Enable_Attribute_Relevance_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep); 
	}

	// 10.25
	/**
	 * Sets the AttributeRelevanceAdvisory (ARA) switch to false. The switch 
	 * state is hold on the RTIG side. That's why the message
	 * DISABLE_ATTRIBUTE_RELEVANCE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the ARA switch is false. When enabling the ARA switch
	 * the federate is informed by the federate service 
	 * turnUpdatesOnForObjectInstance of new object instances within remote 
	 * federates actively subscribed to its published attributes. If there
	 * are no active subscribers for a set of instance-attributes the federate
	 * receives the federate service turnUpdatesOffForObjectInstance. 
	 * @see enableAttributeRelevanceAdvisorySwitch()
	 *
	 * By disabling the ARA switch the federate is no longer informed by 
	 * subscriptions to its published attributes, i.e. the federate 
	 * services turnUpdatesOnForObjectInstance and 
	 * turnUpdatesOffForObjectInstance respectively are not invoked.
	 */
	void RTI1516ambassador::disableAttributeRelevanceAdvisorySwitch ()
		throw (AttributeRelevanceAdvisorySwitchIsOff,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Disable_Attribute_Relevance_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 10.26
	/**
	 * Sets the AttributeScopeAdvisory (ASA) switch to true. The switch state 
	 * is hold on the RTIG side. That's why the message
	 * ENABLE_ATTRIBUTE_SCOPE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the ASA switch is false. When enabling the ASA switch
	 * the federate is informed by the federate services
	 * attributesInScope and attributesOutScope respectively of discovered
	 * or registrated but not owned attribute-instances intersecting or
	 * leaving its subscription regions.
	 *
	 * By disabling the ASA switch the federate is no longer informed of
	 * changes in attribute-instance scope, i.e. the federate 
	 * services attributesInScope and attributesOutScope respectively are 
	 * not invoked.
	 * @see disableAttributeScopeAdvisorySwitch()
	 */
	void RTI1516ambassador::enableAttributeScopeAdvisorySwitch ()
		throw (AttributeScopeAdvisorySwitchIsOn,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Enable_Attribute_Scope_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 10.27
	/**
	 * Sets the AttributeScopeAdvisory (ASA) switch to false. The switch state 
	 * is hold on the RTIG side. That's why the message
	 * DISABLE_ATTRIBUTE_SCOPE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the ASA switch is false. When enabling the ASA switch
	 * the federate is informed by the federate services
	 * attributesInScope and attributesOutScope respectively of discovered
	 * or registrated but not owned attribute-instances intersecting or
	 * leaving its subscription regions.
	 * @see enableAttributeScopeAdvisorySwitch()
	 *
	 * By disabling the ASA switch the federate is no longer informed of
	 * changes in attribute-instance scope, i.e. the federate 
	 * services attributesInScope and attributesOutScope respectively are 
	 * not invoked.
	 */
	void RTI1516ambassador::disableAttributeScopeAdvisorySwitch ()
		throw (AttributeScopeAdvisorySwitchIsOff,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Disable_Attribute_Scope_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 10.28
	/**
	 * Sets the InteractionRelevanceAdvisory (IRA) switch to true. The switch 
	 * state is hold on the RTIG side. That's why the message
	 * ENABLE_INTERACTION_RELEVANCE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the IRA switch is true. This causes a delivery of the
	 * federate service turnInteractionsOn to a publisher if there are 
	 * any new subscribers for the federates published interaction 
	 * classes. If there are no more subscribers a publisher gets the 
	 * federate service turnInteractionsOff(). 
	 *
	 * By disabling the IRA switch the federate is no longer informed by 
	 * subscriptions to its published interaction classes, i.e. the federate 
	 * services turnInteractionsOn and turnInteractionsOff respectively are 
	 * not invoked.
	 * @see disableInteractionRelevanceAdvisorySwitch()
	 */
	void RTI1516ambassador::enableInteractionRelevanceAdvisorySwitch ()
		throw (InteractionRelevanceAdvisorySwitchIsOn,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Enable_Interaction_Relevance_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep);
	}

	// 10.29
	/**
	 * Sets the InteractionRelevanceAdvisory (IRA) switch to false. The switch 
	 * state is hold on the RTIG side. That's why the message
	 * DISABLE_INTERACTION_RELEVANCE_ADVISORY_SWITCH 
	 * is transmitted to RTIA. RTIA transmits the message towards RTIG.
	 *
	 * By default, the IRA switch is true. This causes a delivery of the
	 * federate service turnInteractionsOn to a publisher if there are 
	 * any new subscribers for the federates published interaction 
	 * classes. If there are no more subscribers a publisher gets the 
	 * federate service turnInteractionsOff(). 
	 * @see enableInteractionRelevanceAdvisorySwitch()
	 *
	 * By disabling the IRA switch the federate is no longer informed by 
	 * subscriptions to its published interaction classes, i.e. the federate 
	 * services turnInteractionsOn and turnInteractionsOff respectively are 
	 * not invoked.
	 */
	void RTI1516ambassador::disableInteractionRelevanceAdvisorySwitch ()
		throw (InteractionRelevanceAdvisorySwitchIsOff,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		M_Disable_Interaction_Relevance_Advisory_Switch req, rep ;
		privateRefs->executeService(&req, &rep); 
	}

	// 10.30

	DimensionHandleSet RTI1516ambassador::getDimensionHandleSet
		(RegionHandle theRegionHandle)
		throw (InvalidRegion,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 10.31

	RangeBounds RTI1516ambassador::getRangeBounds
		(RegionHandle theRegionHandle,
		DimensionHandle theDimensionHandle)
		throw (InvalidRegion,
		RegionDoesNotContainSpecifiedDimension,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 10.32
	void RTI1516ambassador::setRangeBounds
		(RegionHandle theRegionHandle,
		DimensionHandle theDimensionHandle,
		RangeBounds const & theRangeBounds)
		throw (InvalidRegion,
		RegionNotCreatedByThisFederate,
		RegionDoesNotContainSpecifiedDimension,
		InvalidRangeBound,
		FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented"); 
	}

	// 10.33
	unsigned long RTI1516ambassador::normalizeFederateHandle
		(FederateHandle theFederateHandle)
		throw (FederateNotExecutionMember,
		InvalidFederateHandle,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 10.34
	unsigned long RTI1516ambassador::normalizeServiceGroup
		(ServiceGroupIndicator theServiceGroup)
		throw (FederateNotExecutionMember,
		InvalidServiceGroup,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 10.37
	bool RTI1516ambassador::evokeCallback(double approximateMinimumTimeInSeconds)
		throw (FederateNotExecutionMember,
		RTIinternalError)
	{ 
		return __tick_kernel(false, approximateMinimumTimeInSeconds, approximateMinimumTimeInSeconds);
	}

	// 10.38
	bool RTI1516ambassador::evokeMultipleCallbacks(double approximateMinimumTimeInSeconds,
		double approximateMaximumTimeInSeconds)
		throw (FederateNotExecutionMember,
		RTIinternalError)
	{ 
		return __tick_kernel(true, approximateMinimumTimeInSeconds, approximateMaximumTimeInSeconds);
	}

	// 10.39
	void RTI1516ambassador::enableCallbacks ()
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	// 10.40
	void RTI1516ambassador::disableCallbacks ()
		throw (FederateNotExecutionMember,
		SaveInProgress,
		RestoreInProgress,
		RTIinternalError)
	{ 
		/* TODO */ 
		throw RTIinternalError(L"Not yet implemented");
	}

	FederateHandle RTI1516ambassador::decodeFederateHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::FederateHandleFriend::createRTI1516Handle(encodedValue);
	}

	ObjectClassHandle RTI1516ambassador::decodeObjectClassHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::ObjectClassHandleFriend::createRTI1516Handle(encodedValue);
	}

	InteractionClassHandle RTI1516ambassador::decodeInteractionClassHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::InteractionClassHandleFriend::createRTI1516Handle(encodedValue);
	}

	ObjectInstanceHandle RTI1516ambassador::decodeObjectInstanceHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::ObjectInstanceHandleFriend::createRTI1516Handle(encodedValue);
	}

	AttributeHandle RTI1516ambassador::decodeAttributeHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::AttributeHandleFriend::createRTI1516Handle(encodedValue);
	}

	ParameterHandle RTI1516ambassador::decodeParameterHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::ParameterHandleFriend::createRTI1516Handle(encodedValue);
	}

	DimensionHandle RTI1516ambassador::decodeDimensionHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::DimensionHandleFriend::createRTI1516Handle(encodedValue);
	}

	MessageRetractionHandle RTI1516ambassador::decodeMessageRetractionHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::MessageRetractionHandleFriend::createRTI1516Handle(encodedValue);
	}

	RegionHandle RTI1516ambassador::decodeRegionHandle(
		VariableLengthData const & encodedValue) const
	{ 
		return rti1516::RegionHandleFriend::createRTI1516Handle(encodedValue);
	}





} // end namespace rti1516