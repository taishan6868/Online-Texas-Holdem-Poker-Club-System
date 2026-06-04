#include "OuterFactoryImp.h"
#include "XGameHttp.pb.h"
#include "LogComm.h"

using namespace tars;
using namespace XGameHttp;

OuterFactoryImpPtr g_objOuterPrx;

OuterFactoryImp::OuterFactoryImp() : _pFileConf(NULL), m_isOpen(0), jsonStr("")
{
    createAllObject();
}

OuterFactoryImp::~OuterFactoryImp()
{
    deleteAllObject();
}

void OuterFactoryImp::deleteAllObject()
{
    if (_pFileConf)
    {
        delete _pFileConf;
        _pFileConf = NULL;
    }
}

//加载配置
int OuterFactoryImp::loadConfig()
{
    __TRY__

    //本地配置文件
    _pFileConf = new tars::TC_Config();
    _pFileConf->parseFile(ServerConfig::BasePath + ServerConfig::ServerName + ".conf");
    LOG_DEBUG << "init config file succ:" << ServerConfig::BasePath + ServerConfig::ServerName + ".conf" << endl;

    //获取UrlKey和对应的OBJ地址
    vector<string> vTransmitDomainKey = (*_pFileConf).getDomainKey("/Main/TransmitObj");
    if (vTransmitDomainKey.size() == 0)
    {
        return 0;
    }

    //清理，重新赋值
    _mTransMitSvrIdObjMap.clear();
    for (size_t i = 0; i < vTransmitDomainKey.size(); ++i)
    {
        string sUrlKey = vTransmitDomainKey[i];
        string strObj = (*_pFileConf).get(string("/Main/TransmitObj/<") + vTransmitDomainKey[i] + ">", "");
        _mTransMitSvrIdObjMap[sUrlKey] = strObj;
        FDLOG_CONFIG_INFO << "load UrlKey : " << sUrlKey << ", strObj : " << strObj << endl;
    }

    //服务开关
    m_isOpen = S2I((*_pFileConf).get("/Main/Maintain<isOpen>", "0"));

    _OrderObjName = (*_pFileConf).get("/Main/<OrderObj>", "");

    __CATCH__
    return 0;
}

//数据库代理服务代理
const OrderServantPrx OuterFactoryImp::getOrderServantPrx(const long uid)
{
    if (!_OrderServerPrx)
    {
        _OrderServerPrx = Application::getCommunicator()->stringToProxy<order::OrderServantPrx>(_OrderObjName);
        ROLLLOG_DEBUG << "Init _OrderObjName succ, _OrderObjName:" << _OrderObjName << endl;
    }

    if (_OrderServerPrx)
    {
        return _OrderServerPrx->tars_hash(uid);
    }

    return NULL;
}

//加载JSON文件
int OuterFactoryImp::loadJsonFile()
{
    std::string sFile = ServerConfig::BasePath + "App.json";
    LOG_ERROR << "read file(" + sFile + ")..... " << endl;

    std::ifstream ifs(sFile);
    std::string content((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
    ifs.close();
    jsonStr = content;
    LOG_ERROR << "jsonStr= " << jsonStr << endl;
    return 0;
}

//加载JSON文件
int OuterFactoryImp::loadReserveJsonFile()
{
    std::string sFile = ServerConfig::BasePath + "ResApp.json";
    LOG_ERROR << "read file(" + sFile + ")..... " << endl;

    std::ifstream ifs(sFile);
    std::string content((istreambuf_iterator<char>(ifs)), (istreambuf_iterator<char>()));
    ifs.close();
    reserveJsonStr = content;
    LOG_ERROR << "reserveJsonStr= " << reserveJsonStr << endl;
    return 0;
}


//
int OuterFactoryImp::isOpen()
{
    return m_isOpen;
}

//
std::string &OuterFactoryImp::getJsonContent()
{
    return jsonStr;
}

//
std::string &OuterFactoryImp::getReserveJsonContent()
{
    return reserveJsonStr;
}

void OuterFactoryImp::createAllObject()
{
    __TRY__

    deleteAllObject();

    //tars代理Factory,访问其他tars接口时使用
    _pProxyFactory = new OuterProxyFactory();
    LOG_DEBUG << "init proxy factory succ." << endl;

    //读取配置
    loadConfig();
    //加载JSON
    loadJsonFile();
    //加载备用JSON文件
    loadReserveJsonFile();

    __CATCH__
    return;
}

void OuterFactoryImp::asyncRequestGameHttp(TarsCurrentPtr current, TC_HttpRequest &req, vector<char> &reqData,
        map<string, std::string> &extInfo, string &urlKey, bool hasCallback)
{
    LOG_DEBUG << "asyncRequestGameHttp: url: " << urlKey << ", uid: " << current->getUId() << ", hasCallback: " << hasCallback
              << ", remoteip: " << extInfo["RemoteIp"] << ", visiturl: " << extInfo["VisitUrl"] << endl;

    XGameHttp::THttpPackage tHttpPkt;
    pbToObj(reqData, tHttpPkt);

    LOG_DEBUG << "reqData size: "<< reqData.size()<< ", tHttpPkt: "<< logPb(tHttpPkt)<< endl;

    auto cb = new RequestGameHttpCallback(current, req);
    auto prx = getGameHttpPrx(urlKey);
    if (!prx)
    {
        LOG_ERROR << "get game http prx null, url: " << urlKey << endl;
        return;
    }

    if (hasCallback)
        prx->tars_hash(tHttpPkt.stuid().luid())->async_doRequest(cb, reqData, extInfo);
    else
        prx->tars_hash(tHttpPkt.stuid().luid())->async_doRequest(NULL, reqData, extInfo);
}

void RequestGameHttpCallback::callback_doRequest(tars::Int32 ret, const vector<tars::Char> &rspBuf)
{
    LOG_DEBUG  << "|GameHttp.doRequest, ret = " << ret << ", rspBuf size: " << rspBuf.size() << endl;

    TC_HttpResponse rsp;
    rsp.setResponse(200, "OK", &rspBuf[0], rspBuf.size());
    rsp.setContentType("image/jpeg");
    g_app.sendHttpResponse(m_current, m_req, rsp);
}

void RequestGameHttpCallback::callback_doRequest_exception(tars::Int32 ret)
{
    LOG_DEBUG << "|GameHttp.doRequest.exception, ret = " << ret << endl;
    g_app.sendErrorPage(m_current, m_req, 500);
}

//钱包订单
void OuterFactoryImp::asyncWalletOrder(TarsCurrentPtr current, TC_HttpRequest &req, string &reqData, string &urlKey)
{
    auto cb = new AsyncOrderCallback(current, req, getOrderServantPrx());
    getOrderServantPrx()->async_doRequestJson(cb, reqData, urlKey);
}

void AsyncOrderCallback::callback_doRequestJson(tars::Int32 ret, const string &rspBuf)
{
    LOG_DEBUG  << "|GameHttp.doRequest, ret = " << ret << ", rspBuf size: " << rspBuf.size() << endl;

    string sPostRsp;

    int code = ret == 0 ? 200 : 500;
    ostringstream os;
    os << "{\"code\":" << code << ",\"msg\":";
    if(ret == 0)
    {
        os << "\"success\"}"<< endl;
    }
    else
    {
        os << "\"绑定或解绑超时，请重试\"}"<< endl;
    }
    //os << "{\"code\":" << code << ",\"msg\":\"success\"}" << endl;
    sPostRsp = os.str();

    TC_HttpResponse rsp;
    rsp.setResponse(200, "OK", sPostRsp.c_str(), sPostRsp.size());
    rsp.setContentType("application/x-www-form-urlencoded");
    g_app.sendHttpResponse(m_current, m_req, rsp);

    if(ret == 0 && pImp && !rspBuf.empty())
    {
        pImp->async_bindWalletNotify(NULL, S2L(rspBuf));
    }
}

void AsyncOrderCallback::callback_doRequestJson_exception(tars::Int32 ret)
{
    LOG_DEBUG << "|GameHttp.doRequest.exception, ret = " << ret << endl;
    g_app.sendErrorPage(m_current, m_req, 500);
}

