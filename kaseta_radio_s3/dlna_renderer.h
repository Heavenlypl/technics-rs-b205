#pragma once
#include <WebServer.h>
#include <WiFiUdp.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "radio_audio.h"
#include "tape_counter.h"
#include "dlna_scpd.h"
// UPnP MediaRenderer for Music Assistant HTTP MP3 and native FLAC streams.
// Event subscriptions explicitly return 412; MA then uses its supported polling fallback.
static WebServer rendererHTTP(8080);
static std::atomic<uint32_t> displayFpsTenths{0};
static WiFiUDP rendererSSDP;
static String rendererUUID,rendererURI,rendererMetadata;
static uint32_t rendererBootId;
// Updated and served by rendererTask.
static uint32_t rendererSelections=0,rendererPlays=0,rendererStops=0;
static const char *rendererServices[]={"AVTransport","RenderingControl","ConnectionManager"};
static constexpr char rendererMP3[]="http-get:*:audio/mpeg:*";
static constexpr char rendererFLAC[]="http-get:*:audio/flac:*";
static const char *rendererTypes[]={"upnp:rootdevice","urn:schemas-upnp-org:device:MediaRenderer:1","urn:schemas-upnp-org:service:AVTransport:1","urn:schemas-upnp-org:service:RenderingControl:1","urn:schemas-upnp-org:service:ConnectionManager:1"};
static String xmlEscape(String s){s.replace("&","&amp;");s.replace("<","&lt;");s.replace(">","&gt;");s.replace("\"","&quot;");s.replace("'","&apos;");return s;}
static String xmlUnescape(String s){s.replace("&lt;","<");s.replace("&gt;",">");s.replace("&quot;","\"");s.replace("&apos;","'");s.replace("&amp;","&");return s;}
static String xmlValue(const String &s,const char *name){String tag=String("<")+name+">",end=String("</")+name+">";int a=s.indexOf(tag);if(a<0)return "";a+=tag.length();int b=s.indexOf(end,a);return b>=a?xmlUnescape(s.substring(a,b)):String();}
static String xmlField(const char *name,const String &value){return String("<")+name+">"+xmlEscape(value)+"</"+name+">";}
static const char *rendererTransportState(){
 const PlaybackMode mode=audioMode.load();
 if(mode==PlaybackMode::Stop)return "STOPPED";
 if(mode==PlaybackMode::Pause)return "PAUSED_PLAYBACK";
 if(audioState.load()==2)return "TRANSITIONING";
 if(audioState.load()!=3)return "STOPPED";
 return "PLAYING";
}
static void rendererSOAP(const String &service,const String &action,const String &body){
 rendererHTTP.send(200,"text/xml; charset=\"utf-8\"",String("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:")+action+"Response xmlns:u=\"urn:schemas-upnp-org:service:"+service+":1\">"+body+"</u:"+action+"Response></s:Body></s:Envelope>");
}
static void rendererError(int code,const char *message){
 rendererHTTP.send(500,"text/xml; charset=\"utf-8\"",String("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\"><s:Body><s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring><detail><UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\"><errorCode>")+code+"</errorCode><errorDescription>"+message+"</errorDescription></UPnPError></detail></s:Fault></s:Body></s:Envelope>");
}
static void rendererControl(){
 String header=rendererHTTP.header("SOAPACTION");header.replace("\"","");int hash=header.indexOf('#');if(hash<0){rendererError(401,"Invalid Action");return;}
 String service=header.substring(0,hash);int colon=service.lastIndexOf(':');service=service.substring(0,colon);service=service.substring(service.lastIndexOf(':')+1);
 String action=header.substring(hash+1),body=rendererHTTP.arg("plain");
 if(body.length()>12288){rendererError(402,"Invalid Args");return;}
 String instance=xmlValue(body,"InstanceID");if(instance.length()&&instance!="0"){rendererError(718,"Invalid InstanceID");return;}
 String answer;
 if(service=="AVTransport"){
  if(action=="SetAVTransportURI"){
   String uri=xmlValue(body,"CurrentURI");
   String metadata=xmlValue(body,"CurrentURIMetaData");
   String path=uri.substring(0,uri.indexOf('?')<0?uri.length():uri.indexOf('?'));
   path.toLowerCase();
   const bool flac=path.endsWith(".flac")||metadata.indexOf(":audio/flac:")>=0||metadata.indexOf(":audio/x-flac:")>=0;
   if(!radioSetURI(uri.c_str(),flac?AudioCodec::FLAC:AudioCodec::MP3)){rendererError(714,"Only HTTP audio streams supported");return;}
   ++rendererSelections;
   rendererURI=uri;rendererMetadata=metadata;
   // Exact known station metadata may update the label; arbitrary track titles cannot.
   String title=xmlValue(rendererMetadata,"dc:title");
   if(magnetLabelQueue)for(size_t i=0;i<STATION_COUNT;i++)if(title==STATION_PRESETS[i].media||title==STATION_PRESETS[i].speak){int preset=i;xQueueOverwrite(magnetLabelQueue,&preset);break;}
   Serial.println("DLNA: przyjeto adres strumienia MA.");
  }else if(action=="Play"){
   if(!rendererURI.length()){rendererError(701,"No media");return;}
   ++rendererPlays;
   radioRequestMode(PlaybackMode::Play);
  }else if(action=="Pause")radioRequestMode(PlaybackMode::Pause);
  else if(action=="Stop"){++rendererStops;radioRequestMode(PlaybackMode::Stop);}
  else if(action=="GetTransportInfo")answer=xmlField("CurrentTransportState",rendererTransportState())+xmlField("CurrentTransportStatus","OK")+xmlField("CurrentSpeed","1");
  else if(action=="GetCurrentTransportActions")answer=xmlField("Actions",rendererURI.length()?"Play,Pause,Stop":"Stop");
  else if(action=="GetDeviceCapabilities")answer=xmlField("PlayMedia","NETWORK")+xmlField("RecMedia","NOT_IMPLEMENTED")+xmlField("RecQualityModes","NOT_IMPLEMENTED");
  else if(action=="GetTransportSettings")answer=xmlField("PlayMode","NORMAL")+xmlField("RecQualityMode","NOT_IMPLEMENTED");
  else if(action=="GetPositionInfo")answer=xmlField("Track","1")+xmlField("TrackDuration","00:00:00")+xmlField("TrackMetaData",rendererMetadata)+xmlField("TrackURI",rendererURI)+xmlField("RelTime","00:00:00")+xmlField("AbsTime","00:00:00")+xmlField("RelCount","0")+xmlField("AbsCount","0");
  else if(action=="GetMediaInfo")answer=xmlField("NrTracks",rendererURI.length()?"1":"0")+xmlField("MediaDuration","00:00:00")+xmlField("CurrentURI",rendererURI)+xmlField("CurrentURIMetaData",rendererMetadata)+xmlField("NextURI","")+xmlField("NextURIMetaData","")+xmlField("PlayMedium","NETWORK")+xmlField("RecordMedium","NOT_IMPLEMENTED")+xmlField("WriteStatus","NOT_IMPLEMENTED");
  else {rendererError(401,"Invalid Action");return;}
 }else if(service=="RenderingControl"){
  String channel=xmlValue(body,"Channel");if(channel.length()&&channel!="Master"){rendererError(402,"Invalid Channel");return;}
  if(action=="GetVolume")answer=xmlField("CurrentVolume",String(audioVolume.load()));
  else if(action=="SetVolume"){String v=xmlValue(body,"DesiredVolume");int n=v.toInt();if(n<0||n>100){rendererError(402,"Invalid volume");return;}audioVolume.store(n);}
  else if(action=="GetMute")answer=xmlField("CurrentMute",audioMute.load()?"1":"0");
  else if(action=="SetMute"){String value=xmlValue(body,"DesiredMute");if(value!="0"&&value!="1"){rendererError(402,"Invalid mute");return;}audioMute.store(value=="1");}
  else {rendererError(401,"Invalid Action");return;}
 }else if(service=="ConnectionManager"){
  if(action=="GetProtocolInfo")answer=xmlField("Source","")+xmlField("Sink",String(rendererMP3)+","+rendererFLAC);
  else if(action=="GetCurrentConnectionIDs")answer=xmlField("ConnectionIDs","0");
  else if(action=="GetCurrentConnectionInfo")answer=xmlField("RcsID","0")+xmlField("AVTransportID","0")+xmlField("ProtocolInfo",audioCodec.load()==AudioCodec::FLAC?rendererFLAC:rendererMP3)+xmlField("PeerConnectionManager","")+xmlField("PeerConnectionID","-1")+xmlField("Direction","Input")+xmlField("Status","OK");
  else {rendererError(401,"Invalid Action");return;}
 }else{rendererError(401,"Invalid Service");return;}
 rendererSOAP(service,action,answer);
}
static String rendererDescription(){
 String ip=WiFi.localIP().toString();
 String s="<?xml version=\"1.0\"?><root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><URLBase>http://"+ip+":8080/</URLBase><device><deviceType>urn:schemas-upnp-org:device:MediaRenderer:1</deviceType><friendlyName>Technics</friendlyName><manufacturer>Technics DIY</manufacturer><modelName>RS-B205 ESP32-S3</modelName><modelNumber>1</modelNumber><serialNumber>"+String(magnetMac)+"</serialNumber><UDN>"+rendererUUID+"</UDN><serviceList>";
 for(const char *type:rendererServices)s+=String("<service><serviceType>urn:schemas-upnp-org:service:")+type+":1</serviceType><serviceId>urn:upnp-org:serviceId:"+type+"</serviceId><SCPDURL>/upnp/"+type+".xml</SCPDURL><controlURL>/upnp/control/"+type+"</controlURL><eventSubURL>/upnp/event/"+type+"</eventSubURL></service>";
 return s+"</serviceList><presentationURL>http://"+ip+":8080/status</presentationURL></device></root>";
}
enum class RendererAdvertisement { Reply, Alive, Byebye };
static void rendererResponse(const String &st,IPAddress address,uint16_t port,RendererAdvertisement kind=RendererAdvertisement::Reply){
 String usn=rendererUUID;if(st!=rendererUUID)usn+="::"+st;
 bool reply=kind==RendererAdvertisement::Reply;
 String text=reply?"HTTP/1.1 200 OK\r\n":"NOTIFY * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n";
 if(kind!=RendererAdvertisement::Byebye)
  text+="CACHE-CONTROL: max-age=1800\r\nLOCATION: http://"+WiFi.localIP().toString()+":8080/description.xml\r\nSERVER: ESP32/1.0 UPnP/1.0 Technics/8\r\n";
 text+="BOOTID.UPNP.ORG: "+String(rendererBootId)+"\r\n";
 if(reply)text+="EXT:\r\nST: "+st+"\r\n";
 else text+="NT: "+st+"\r\nNTS: "+(kind==RendererAdvertisement::Alive?"ssdp:alive":"ssdp:byebye")+"\r\n";
 text+="USN: "+usn+"\r\n\r\n";
 rendererSSDP.beginPacket(address,port);rendererSSDP.write(reinterpret_cast<const uint8_t *>(text.c_str()),text.length());rendererSSDP.endPacket();
}
static void rendererAdvertise(RendererAdvertisement kind){
 const IPAddress group(239,255,255,250);
 rendererResponse(rendererUUID,group,1900,kind);
 for(const char *type:rendererTypes)rendererResponse(type,group,1900,kind);
}
static void rendererTask(void *){
 static constexpr uint32_t announcementGapsMs[]={300,1700,3000,5000,60000};
 uint32_t announce=0;size_t gap=0;bool started=false,ssdpReady=false;
 for(;;){
  if(WiFi.status()!=WL_CONNECTED){if(started){rendererHTTP.stop();rendererSSDP.stop();started=false;ssdpReady=false;}vTaskDelay(pdMS_TO_TICKS(100));continue;}
  if(!started){
   rendererHTTP.begin();started=true;gap=0;announce=millis();
   Serial.printf("DLNA: Technics http://%s:8080/description.xml\n",WiFi.localIP().toString().c_str());
  }
  rendererHTTP.handleClient();
  if(!ssdpReady){
   ssdpReady=rendererSSDP.beginMulticast(IPAddress(239,255,255,250),1900);
   if(!ssdpReady){vTaskDelay(pdMS_TO_TICKS(100));continue;}
   // A hard power-off cannot send byebye. Clear stale discovery entries
   // before announcing the same UDN again.
   rendererAdvertise(RendererAdvertisement::Byebye);
   announce=millis();
  }
  int bytes=rendererSSDP.parsePacket();
  if(bytes>0){
   char request[1200];int n=rendererSSDP.read(request,sizeof(request)-1);if(n<0)n=0;request[n]=0;String q=request;
   String lower=q;lower.toLowerCase();
   if(q.startsWith("M-SEARCH")&&lower.indexOf("ssdp:discover")>=0){
    int a=lower.indexOf("\r\nst:");
    if(a>=0){a+=5;int b=q.indexOf("\r\n",a);String st=q.substring(a,b);st.trim();
     IPAddress remote=rendererSSDP.remoteIP();uint16_t port=rendererSSDP.remotePort();
     if(st=="ssdp:all"||st==rendererUUID)rendererResponse(rendererUUID,remote,port);
     for(const char *type:rendererTypes)if(st=="ssdp:all"||st==type)rendererResponse(type,remote,port);
    }
   }
  }
  if(uint32_t(millis()-announce)>=announcementGapsMs[gap]){
   announce=millis();rendererAdvertise(RendererAdvertisement::Alive);
   if(gap+1<sizeof(announcementGapsMs)/sizeof(announcementGapsMs[0]))++gap;
  }
  vTaskDelay(pdMS_TO_TICKS(5));
 }
}
static void startDLNARenderer(){
 rendererUUID=String("uuid:8f4db205-0000-4000-8000-")+magnetMac;
 // The UDN remains stable, but a new boot must be visible to discovery caches.
 Preferences ssdp;
 rendererBootId=(esp_random()&0x3fffffffU)+1;
 if(ssdp.begin("technics-ssdp")){
  const uint32_t previous=ssdp.getUInt("boot",0);
  if(previous)rendererBootId=(previous&0x3fffffffU)+1;
  ssdp.putUInt("boot",rendererBootId);
  ssdp.end();
 }
 static const char *headers[]={"SOAPACTION"};rendererHTTP.collectHeaders(headers,1);
 rendererHTTP.on("/description.xml",HTTP_GET,[]{rendererHTTP.send(200,"text/xml",rendererDescription());});
 rendererHTTP.on("/upnp/AVTransport.xml",HTTP_GET,[]{rendererHTTP.send_P(200,"text/xml",DLNA_AVTransport);});
 rendererHTTP.on("/upnp/RenderingControl.xml",HTTP_GET,[]{rendererHTTP.send_P(200,"text/xml",DLNA_RenderingControl);});
 rendererHTTP.on("/upnp/ConnectionManager.xml",HTTP_GET,[]{rendererHTTP.send_P(200,"text/xml",DLNA_ConnectionManager);});
 for(const char *type:rendererServices){
  rendererHTTP.on(String("/upnp/control/")+type,HTTP_POST,rendererControl);
  rendererHTTP.on(String("/upnp/event/")+type,HTTP_ANY,[]{rendererHTTP.send(412,"text/plain","Use polling");});
 }
 rendererHTTP.on("/status",HTTP_GET,[]{
  String host=rendererURI;int a=host.indexOf("://");if(a>=0)host=host.substring(a+3);int end=host.indexOf('/');if(end>=0)host=host.substring(0,end);
  const int seek=tapeSeekDirection();
  String status=String("{\"name\":\"Technics\",\"version\":26,\"stream_host\":\"")+host+
   "\",\"mode\":\""+radioPlaybackState()+"\",\"receiving\":"+(radioIsReceiving()?"true":"false")+
   ",\"codec\":\""+(audioCodec.load()==AudioCodec::FLAC?"flac":"mp3")+"\""+
   ",\"bitrate\":"+audioBitrate.load()+",\"rate\":"+audioRate.load()+",\"samples\":"+audioSamples.load()+
   ",\"connections\":"+audioConnections.load()+",\"underflows\":"+audioUnderflows.load()+
   ",\"heap\":"+ESP.getFreeHeap()+",\"fps_x10\":"+displayFpsTenths.load()+
   ",\"audio_buffer\":"+audioFill.load()+",\"audio_buffer_capacity\":"+audioBufferCapacity.load()+
   ",\"muted_samples\":"+audioMutedSamples.load()+",\"audio_stack_free\":"+audioStackFree.load()+
   ",\"tape_counter_active\":"+(tapeCounterActive.load()?"true":"false")+
   ",\"tape_counter_steps\":"+tapeCounterSteps.load()+
   ",\"tape_seek\":\""+(seek>0?"forward":seek<0?"reverse":"idle")+"\""+
   ",\"prev_presses\":"+magnetPrevPresses.load()+",\"next_presses\":"+magnetNextPresses.load()+
   ",\"dlna_set_uri\":"+rendererSelections+",\"dlna_play\":"+rendererPlays+",\"dlna_stop\":"+rendererStops+
   ",\"uptime_s\":"+static_cast<unsigned long>(esp_timer_get_time()/1000000)+
   ",\"reset_reason\":"+static_cast<int>(esp_reset_reason())+"}";
  rendererHTTP.send(200,"application/json",status);
 });
 if(xTaskCreatePinnedToCore(rendererTask,"technics_dlna",8192,nullptr,1,nullptr,0)!=pdPASS)Serial.println("BLAD: DLNA task allocation");
}
