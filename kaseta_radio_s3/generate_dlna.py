from pathlib import Path
from xml.sax.saxutils import escape
services={}
def service(key,variables,actions):
 s='<?xml version="1.0"?><scpd xmlns="urn:schemas-upnp-org:service-1-0"><specVersion><major>1</major><minor>0</minor></specVersion><actionList>'
 for name,args in actions.items():
  s+='<action><name>'+name+'</name>'
  if args:s+='<argumentList>'+''.join('<argument><name>'+n+'</name><direction>'+d+'</direction><relatedStateVariable>'+v+'</relatedStateVariable></argument>' for n,d,v in args)+'</argumentList>'
  s+='</action>'
 s+='</actionList><serviceStateTable>'
 for name,typ,values in variables:
  s+='<stateVariable sendEvents="no"><name>'+name+'</name><dataType>'+typ+'</dataType>'
  if values:s+='<allowedValueList>'+''.join('<allowedValue>'+escape(v)+'</allowedValue>' for v in values)+'</allowedValueList>'
  s+='</stateVariable>'
 services[key]=s+'</serviceStateTable></scpd>'
def arg(n,v=None,d='out'):return(n,d,v or n)
def avargs(*out):return [arg('InstanceID','A_ARG_TYPE_InstanceID','in')]+[arg(*o) if isinstance(o,tuple) else arg(o) for o in out]
avvars=[('A_ARG_TYPE_InstanceID','ui4',[]),('TransportState','string',['STOPPED','PLAYING','PAUSED_PLAYBACK','TRANSITIONING','NO_MEDIA_PRESENT']),('TransportStatus','string',['OK','ERROR_OCCURRED']),('TransportPlaySpeed','string',['1']),('CurrentTransportActions','string',[]),('PlaybackStorageMedium','string',['NETWORK']),('RecordStorageMedium','string',['NOT_IMPLEMENTED']),('PossiblePlaybackStorageMedia','string',['NETWORK']),('PossibleRecordStorageMedia','string',['NOT_IMPLEMENTED']),('CurrentPlayMode','string',['NORMAL']),('CurrentRecordQualityMode','string',['NOT_IMPLEMENTED']),('PossibleRecordQualityModes','string',['NOT_IMPLEMENTED']),('RecordMediumWriteStatus','string',['NOT_IMPLEMENTED'])]
for n in ['CurrentTrackDuration','CurrentMediaDuration','CurrentTrackMetaData','CurrentTrackURI','AVTransportURI','AVTransportURIMetaData','NextAVTransportURI','NextAVTransportURIMetaData','RelativeTimePosition','AbsoluteTimePosition']:avvars.append((n,'string',[]))
for n in ['NumberOfTracks','CurrentTrack']:avvars.append((n,'ui4',[]))
for n in ['RelativeCounterPosition','AbsoluteCounterPosition']:avvars.append((n,'i4',[]))
service('AVTransport',avvars,{
 'SetAVTransportURI':avargs(('CurrentURI','AVTransportURI','in'),('CurrentURIMetaData','AVTransportURIMetaData','in')),
 'Play':avargs(('Speed','TransportPlaySpeed','in')),'Pause':avargs(),'Stop':avargs(),
 'GetTransportInfo':avargs(('CurrentTransportState','TransportState'),('CurrentTransportStatus','TransportStatus'),('CurrentSpeed','TransportPlaySpeed')),
 'GetPositionInfo':avargs(('Track','CurrentTrack'),('TrackDuration','CurrentTrackDuration'),('TrackMetaData','CurrentTrackMetaData'),('TrackURI','CurrentTrackURI'),('RelTime','RelativeTimePosition'),('AbsTime','AbsoluteTimePosition'),('RelCount','RelativeCounterPosition'),('AbsCount','AbsoluteCounterPosition')),
 'GetMediaInfo':avargs(('NrTracks','NumberOfTracks'),('MediaDuration','CurrentMediaDuration'),('CurrentURI','AVTransportURI'),('CurrentURIMetaData','AVTransportURIMetaData'),('NextURI','NextAVTransportURI'),('NextURIMetaData','NextAVTransportURIMetaData'),('PlayMedium','PlaybackStorageMedium'),('RecordMedium','RecordStorageMedium'),('WriteStatus','RecordMediumWriteStatus')),
 'GetDeviceCapabilities':avargs(('PlayMedia','PossiblePlaybackStorageMedia'),('RecMedia','PossibleRecordStorageMedia'),('RecQualityModes','PossibleRecordQualityModes')),
 'GetTransportSettings':avargs(('PlayMode','CurrentPlayMode'),('RecQualityMode','CurrentRecordQualityMode')),
 'GetCurrentTransportActions':avargs(('Actions','CurrentTransportActions'))})
rcargs=lambda *args:[arg('InstanceID','A_ARG_TYPE_InstanceID','in'),arg('Channel','A_ARG_TYPE_Channel','in')]+list(args)
service('RenderingControl',[('A_ARG_TYPE_InstanceID','ui4',[]),('A_ARG_TYPE_Channel','string',['Master']),('Volume','ui2',[]),('Mute','boolean',[])],{
 'GetVolume':rcargs(arg('CurrentVolume','Volume')),'SetVolume':rcargs(arg('DesiredVolume','Volume','in')),
 'GetMute':rcargs(arg('CurrentMute','Mute')),'SetMute':rcargs(arg('DesiredMute','Mute','in'))})
cmvars=[('SourceProtocolInfo','string',[]),('SinkProtocolInfo','string',[]),('CurrentConnectionIDs','string',[]),('A_ARG_TYPE_ConnectionID','i4',[]),('A_ARG_TYPE_RcsID','i4',[]),('A_ARG_TYPE_AVTransportID','i4',[]),('A_ARG_TYPE_ProtocolInfo','string',[]),('A_ARG_TYPE_ConnectionManager','string',[]),('A_ARG_TYPE_Direction','string',['Input','Output']),('A_ARG_TYPE_ConnectionStatus','string',['OK','Unknown'])]
service('ConnectionManager',cmvars,{'GetProtocolInfo':[arg('Source','SourceProtocolInfo'),arg('Sink','SinkProtocolInfo')],'GetCurrentConnectionIDs':[arg('ConnectionIDs','CurrentConnectionIDs')],'GetCurrentConnectionInfo':[arg('ConnectionID','A_ARG_TYPE_ConnectionID','in')]+[arg(n,'A_ARG_TYPE_'+v) for n,v in [('RcsID','RcsID'),('AVTransportID','AVTransportID'),('ProtocolInfo','ProtocolInfo'),('PeerConnectionManager','ConnectionManager'),('PeerConnectionID','ConnectionID'),('Direction','Direction'),('Status','ConnectionStatus')]]})
out='#pragma once\n#include <pgmspace.h>\n'
for key,xml in services.items():out+='static const char DLNA_'+key+'[] PROGMEM=R"XML('+xml+')XML";\n'
Path(__file__).with_name('dlna_scpd.h').write_text(out)
print('Generated 3 UPnP SCPD documents:',sum(map(len,services.values())),'bytes in flash')
