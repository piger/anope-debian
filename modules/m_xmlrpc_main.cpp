#include "module.h"
#include "modules/xmlrpc.h"

static Module *me;

class XMLRPCIdentifyRequest : public IdentifyRequest
{
	XMLRPCRequest request;
	HTTPReply repl; /* Request holds a reference to the HTTPReply, because we might exist long enough to invalidate it
	                   we'll copy it here then reset the reference before we use it */
	Reference<HTTPClient> client;
	Reference<XMLRPCServiceInterface> xinterface;

 public:
	XMLRPCIdentifyRequest(Module *m, XMLRPCRequest& req, HTTPClient *c, XMLRPCServiceInterface* iface, const Anope::string &acc, const Anope::string &pass) : IdentifyRequest(m, acc, pass), request(req), repl(request.r), client(c), xinterface(iface) { }

	void OnSuccess() anope_override
	{
		if (!xinterface || !client)
			return;

		request.r = this->repl;

		request.reply("result", "Success");
		request.reply("account", GetAccount());

		xinterface->Reply(request);
		client->SendReply(&request.r);
	}

	void OnFail() anope_override
	{
		if (!xinterface || !client)
			return;

		request.r = this->repl;

		request.reply("error", "Invalid password");

		xinterface->Reply(request);
		client->SendReply(&request.r);
	}
};

class MyXMLRPCEvent : public XMLRPCEvent
{
 public:
	bool Run(XMLRPCServiceInterface *iface, HTTPClient *client, XMLRPCRequest &request) anope_override
	{
		if (request.name == "command")
			this->DoCommand(iface, client, request);
		else if (request.name == "checkAuthentication")
			return this->DoCheckAuthentication(iface, client, request);
		else if (request.name == "stats")
			this->DoStats(iface, client, request);
		else if (request.name == "channel")
			this->DoChannel(iface, client, request);
		else if (request.name == "user")
			this->DoUser(iface, client, request);
		else if (request.name == "opers")
			this->DoOperType(iface, client, request);

		return true;
	}

 private:
	void DoCommand(XMLRPCServiceInterface *iface, HTTPClient *client, XMLRPCRequest &request)
	{
		Anope::string service = request.data.size() > 0 ? request.data[0] : "";
		Anope::string user = request.data.size() > 1 ? request.data[1] : "";
		Anope::string command = request.data.size() > 2 ? request.data[2] : "";

		if (service.empty() || user.empty() || command.empty())
			request.reply("error", "Invalid parameters");
		else
		{
			BotInfo *bi = BotInfo::Find(service, true);
			if (!bi)
				request.reply("error", "Invalid service");
			else
			{
				request.reply("result", "Success");

				NickAlias *na = NickAlias::Find(user);

				Anope::string out;

				struct XMLRPCommandReply : CommandReply
				{
					Anope::string &str;

					XMLRPCommandReply(Anope::string &s) : str(s) { }

					void SendMessage(BotInfo *, const Anope::string &msg) anope_override
					{
						str += msg + "\n";
					};
				}
				reply(out);

				CommandSource source(user, NULL, na ? *na->nc : NULL, &reply, bi);
				Command::Run(source, command);

				if (!out.empty())
					request.reply("return", iface->Sanitize(out));
			}
		}
	}

	bool DoCheckAuthentication(XMLRPCServiceInterface *iface, HTTPClient *client, XMLRPCRequest &request)
	{
		Anope::string username = request.data.size() > 0 ? request.data[0] : "";
		Anope::string password = request.data.size() > 1 ? request.data[1] : "";

		if (username.empty() || password.empty())
			request.reply("error", "Invalid parameters");
		else
		{
			XMLRPCIdentifyRequest *req = new XMLRPCIdentifyRequest(me, request, client, iface, username, password);
			FOREACH_MOD(OnCheckAuthentication, (NULL, req));
			req->Dispatch();
			return false;
		}
		
		return true;
	}

	void DoStats(XMLRPCServiceInterface *iface, HTTPClient *client, XMLRPCRequest &request)
	{
		request.reply("uptime", stringify(Anope::CurTime - Anope::StartTime));
		request.reply("uplinkname", Me->GetLinks().front()->GetName());
		{
			Anope::string buf;
			for (std::set<Anope::string>::iterator it = Servers::Capab.begin(); it != Servers::Capab.end(); ++it)
				buf += " " + *it;
			if (!buf.empty())
				buf.erase(buf.begin());
			request.reply("uplinkcapab", buf);
		}
		request.reply("usercount", stringify(UserListByNick.size()));
		request.reply("maxusercount", stringify(MaxUserCount));
		request.reply("channelcount", stringify(ChannelList.size()));
	}

	void DoChannel(XMLRPCServiceInterface *iface, HTTPClient *client, XMLRPCRequest &request)
	{
		if (request.data.empty())
			return;

		Channel *c = Channel::Find(request.data[0]);

		request.reply("name", iface->Sanitize(c ? c->name : request.data[0]));

		if (c)
		{
			request.reply("bancount", stringify(c->HasMode("BAN")));
			int count = 0;
			std::pair<Channel::ModeList::iterator, Channel::ModeList::iterator> its = c->GetModeList("BAN");
			for (; its.first != its.second; ++its.first)
				request.reply("ban" + stringify(++count), iface->Sanitize(its.first->second));

			request.reply("exceptcount", stringify(c->HasMode("EXCEPT")));
			count = 0;
			its = c->GetModeList("EXCEPT");
			for (; its.first != its.second; ++its.first)
				request.reply("except" + stringify(++count), iface->Sanitize(its.first->second));

			request.reply("invitecount", stringify(c->HasMode("INVITEOVERRIDE")));
			count = 0;
			its = c->GetModeList("INVITEOVERRIDE");
			for (; its.first != its.second; ++its.first)
				request.reply("invite" + stringify(++count), iface->Sanitize(its.first->second));

			Anope::string users;
			for (Channel::ChanUserList::const_iterator it = c->users.begin(); it != c->users.end(); ++it)
			{
				ChanUserContainer *uc = it->second;
				users += uc->status.BuildModePrefixList() + uc->user->nick + " ";
			}
			if (!users.empty())
			{
				users.erase(users.length() - 1);
				request.reply("users", iface->Sanitize(users));
			}

			if (!c->topic.empty())
				request.reply("topic", iface->Sanitize(c->topic));

			if (!c->topic_setter.empty())
				request.reply("topicsetter", iface->Sanitize(c->topic_setter));

			request.reply("topictime", stringify(c->topic_time));
			request.reply("topicts", stringify(c->topic_ts));
		}
	}

	void DoUser(XMLRPCServiceInterface *iface, HTTPClient *client, XMLRPCRequest &request)
	{
		if (request.data.empty())
			return;

		User *u = User::Find(request.data[0]);

		request.reply("nick", iface->Sanitize(u ? u->nick : request.data[0]));

		if (u)
		{
			request.reply("ident", iface->Sanitize(u->GetIdent()));
			request.reply("vident", iface->Sanitize(u->GetVIdent()));
			request.reply("host", iface->Sanitize(u->host));
			if (!u->vhost.empty())
				request.reply("vhost", iface->Sanitize(u->vhost));
			if (!u->chost.empty())
				request.reply("chost", iface->Sanitize(u->chost));
			request.reply("ip", u->ip.addr());
			request.reply("timestamp", stringify(u->timestamp));
			request.reply("signon", stringify(u->signon));
			if (u->Account())
			{
				request.reply("account", iface->Sanitize(u->Account()->display));
				if (u->Account()->o)
					request.reply("opertype", iface->Sanitize(u->Account()->o->ot->GetName()));
			}

			Anope::string channels;
			for (User::ChanUserList::const_iterator it = u->chans.begin(); it != u->chans.end(); ++it)
			{
				ChanUserContainer *cc = it->second;
				channels += cc->status.BuildModePrefixList() + cc->chan->name + " ";
			}
			if (!channels.empty())
			{
				channels.erase(channels.length() - 1);
				request.reply("channels", channels);
			}
		}
	}

	void DoOperType(XMLRPCServiceInterface *iface, HTTPClient *client, XMLRPCRequest &request)
	{
		for (unsigned i = 0; i < Config->MyOperTypes.size(); ++i)
		{
			OperType *ot = Config->MyOperTypes[i];
			Anope::string perms;
			for (std::list<Anope::string>::const_iterator it2 = ot->GetPrivs().begin(), it2_end = ot->GetPrivs().end(); it2 != it2_end; ++it2)
				perms += " " + *it2;
			for (std::list<Anope::string>::const_iterator it2 = ot->GetCommands().begin(), it2_end = ot->GetCommands().end(); it2 != it2_end; ++it2)
				perms += " " + *it2;
			request.reply(ot->GetName(), perms);
		}
	}
};

class ModuleXMLRPCMain : public Module
{
	ServiceReference<XMLRPCServiceInterface> xmlrpc;

	MyXMLRPCEvent stats;

 public:
	ModuleXMLRPCMain(const Anope::string &modname, const Anope::string &creator) : Module(modname, creator, EXTRA | VENDOR), xmlrpc("XMLRPCServiceInterface", "xmlrpc")
	{
		me = this;

		if (!xmlrpc)
			throw ModuleException("Unable to find xmlrpc reference, is m_xmlrpc loaded?");

		xmlrpc->Register(&stats);
	}

	~ModuleXMLRPCMain()
	{
		if (xmlrpc)
			xmlrpc->Unregister(&stats);
	}
};

MODULE_INIT(ModuleXMLRPCMain)

