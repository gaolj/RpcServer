#pragma once
#pragma warning(push)
#pragma warning(disable: 4819)        //warning about encoding of charactor in source code.
#include <boost/type_traits.hpp>
#pragma warning(pop)


#ifdef __GNUC__
#define TYPENAME typename
#else
#define TYPENAME
#endif

#include "Logger.h"
#include "Exception.h"
#include "msgpack/object.hpp"
#include "msgpack/sbuffer.hpp"
#include "boost/format.hpp"
#include "TupleUtil.h"
#include "Protocol.h"			// MsgResponse

#define MSGPACK_CONVERT(obj, arg)\
	try{obj.convert(&arg);}\
	catch (msgpack::type_error& ex)\
	{\
		BOOST_THROW_EXCEPTION(boost::enable_error_info(ex) <<\
			err_no(error_params_convert) <<\
			err_str(std::string("error_params_convert:") + typeid(arg).name()));\
	}

std::string getBoostExceptionThrowLocation(const boost::exception& ex);

class TcpSession;
std::shared_ptr<TcpSession> getCurrentTcpSession();
void setCurrentTcpSession(std::shared_ptr<TcpSession> pSession);

class msgerror : public std::runtime_error
{
	MessageError _code;

public:
	msgerror(const std::string &msg, MessageError code) :std::runtime_error(msg), _code(code)
	{
	}

	std::shared_ptr<msgpack::sbuffer> to_msg(uint32_t msgid)const
	{
		MsgResponse<std::tuple<int, std::string>, bool> msgres(
			std::make_tuple(_code, what()),
			true,
			msgid);

		auto sbuf = std::make_shared<msgpack::sbuffer>();
		msgpack::pack(*sbuf, msgres);
		return sbuf;
	}

};

template<typename F, typename R, typename C, typename TArgs>
std::shared_ptr<msgpack::sbuffer>
helpInvoke(F handler, uint32_t msgid, msgpack::object objArgs)
{
	BOOST_LOG_NAMED_SCOPE("helpInvoke_R1");
	if (objArgs.type != msgpack::type::ARRAY)
		BOOST_THROW_EXCEPTION(ArgsCheckException() << err_no(error_params_not_array) << err_str("error_params_not_array"));
	if (objArgs.via.array.size > std::tuple_size<TArgs>::value)
		BOOST_THROW_EXCEPTION(ArgsCheckException() << err_no(error_params_too_many) << err_str("error_params_too_many"));
	else if (objArgs.via.array.size < std::tuple_size<TArgs>::value)
		BOOST_THROW_EXCEPTION(ArgsCheckException() << err_no(error_params_not_enough) << err_str("error_params_not_enough"));

	TArgs args;
	MSGPACK_CONVERT(objArgs, args);

    // call
    R result=std::call_with_tuple(handler, args);

    MsgResponse<R&, bool> msgres(result, false, msgid);

    // result
    auto sbuf=std::make_shared<msgpack::sbuffer>();
    msgpack::pack(*sbuf, msgres);
    return sbuf;
}

// void
template<typename F, typename C, typename TArgs>
std::shared_ptr<msgpack::sbuffer>
helpInvoke(F handler, uint32_t msgid, msgpack::object objArgs)
{
	BOOST_LOG_NAMED_SCOPE("helpInvoke_R0");
	// args check
    if(objArgs.type != msgpack::type::ARRAY) {
        throw msgerror("error_params_not_array", error_params_not_array); 
    }
    if(objArgs.via.array.size > std::tuple_size<TArgs>::value){
        throw msgerror("error_params_too_many", error_params_too_many); 
    }
    else if(objArgs.via.array.size < std::tuple_size<TArgs>::value){
		throw boost::enable_error_info(msgerror("error_params_not_enough", error_params_not_enough));
    }

    // args extract
    TArgs args;
	MSGPACK_CONVERT(objArgs, args);

    // call
    std::call_with_tuple_void(handler, args);

    MsgResponse<msgpack::type::nil, bool> msgres(msgpack::type::nil(), false, msgid);

    // result
    auto sbuf=std::make_shared<msgpack::sbuffer>();
    msgpack::pack(*sbuf, msgres);
    return sbuf;
}

class TcpSession;
class Dispatcher
{
    typedef std::function<std::shared_ptr<msgpack::sbuffer>(uint32_t, msgpack::object)> Procedure;
    std::map<std::string, Procedure> m_handlerMap;

	src::severity_channel_logger<SeverityLevel> _logger{ keywords::channel = "Dispatcher" };
	//logging::attribute_set::iterator _net_remote_addr;
public:
	Dispatcher() {}

	~Dispatcher() {}

	void dispatch(const msgpack::object &objMsg, msgpack::zone&& zone, std::shared_ptr<TcpSession> session);

	////////////////////
    // 0
    template<typename F, typename R, typename C>
        void add_handler(const std::string &method, F handler, R(C::*p)()const)
        {
			m_handlerMap.insert(std::make_pair(method, [handler](
				uint32_t msgid,
				::msgpack::object msg_params)->std::shared_ptr<msgpack::sbuffer>
				{
					return helpInvoke<F, R, C, std::tuple<>>(
						handler, msgid, msg_params);
				}));
		}

    // 1
    template<typename F, typename R, typename C, typename A1>
        void add_handler(const std::string &method, F handler, R(C::*p)(A1)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
                        return helpInvoke<F, R, C, std::tuple<B1>>(
                            handler, msgid, objArgs);
                        }));
        }

    // 2
    template<typename F, typename R, typename C, 
        typename A1, typename A2>
        void add_handler(const std::string &method, F handler, R(C::*p)(A1, A2)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
			{
				typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
				typedef TYPENAME boost::remove_const<typename boost::remove_reference<A2>::type>::type B2;
				return helpInvoke<F, R, C, std::tuple<B1, B2>>(
					handler, msgid, objArgs);

			}));
        }

    // 3
    template<typename F, typename R, typename C, 
        typename A1, typename A2, typename A3>
        void add_handler(const std::string &method, F handler, R(C::*p)(A1, A2, A3)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A2>::type>::type B2;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A3>::type>::type B3;
                        return helpInvoke<F, R, C, std::tuple<B1, B2, B3>>(
                            handler, msgid, objArgs);

                        }));
        }

    // 4
    template<typename F, typename R, typename C, 
        typename A1, typename A2, typename A3, typename A4>
        void add_handler(const std::string &method, F handler, R(C::*p)(A1, A2, A3, A4)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A2>::type>::type B2;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A3>::type>::type B3;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A4>::type>::type B4;
                        return helpInvoke<F, R, C, std::tuple<B1, B2, B3, B4>>(
                            handler, msgid, objArgs);

                        }));
        }

    // void
    // 0
    template<typename F, typename C
        >
        void add_handler(const std::string &method, F handler, void(C::*p)()const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        return helpInvoke<F, C, std::tuple<>>(
                            handler, msgid, objArgs);

                        }));
        }

    // 1
    template<typename F, typename C, 
        typename A1>
        void add_handler(const std::string &method, F handler, void(C::*p)(A1)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
                        return helpInvoke<F, C, std::tuple<B1>>(
                            handler, msgid, objArgs);

                        }));
        }

    // 2
    template<typename F, typename C, 
        typename A1, typename A2>
        void add_handler(const std::string &method, F handler, void(C::*p)(A1, A2)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A2>::type>::type B2;
                        return helpInvoke<F, C, std::tuple<B1, B2>>(
                            handler, msgid, objArgs);

                        }));
        }

    // 3
    template<typename F, typename C, 
        typename A1, typename A2, typename A3>
        void add_handler(const std::string &method, F handler, void(C::*p)(A1, A2, A3)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A2>::type>::type B2;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A3>::type>::type B3;
                        return helpInvoke<F, C, std::tuple<B1, B2, B3>>(
                            handler, msgid, objArgs);

                        }));
        }

    // 4
    template<typename F, typename C, 
        typename A1, typename A2, typename A3, typename A4>
        void add_handler(const std::string &method, F handler, void(C::*p)(A1, A2, A3, A4)const)
        {
            m_handlerMap.insert(std::make_pair(method, [handler](
                            uint32_t msgid, 
                            ::msgpack::object objArgs)->std::shared_ptr<msgpack::sbuffer>
                        {
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A1>::type>::type B1;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A2>::type>::type B2;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A3>::type>::type B3;
                        typedef TYPENAME boost::remove_const<typename boost::remove_reference<A4>::type>::type B4;
                        return helpInvoke<F, C, std::tuple<B1, B2, B3, B4>>(
                            handler, msgid, objArgs);

                        }));
        }

    // for lambda/std::function
    template<typename F>
        void add_handler(const std::string &method, F handler)
        {
            add_handler(method, handler, &F::operator());
        }

    // for function pointer
    // 0
    template<typename R>
        void add_handler(const std::string &method, R(*handler)())
        {
            add_handler(method, std::function<R()>(handler));
        }
    // 1
    template<typename R, typename A1>
        void add_handler(const std::string &method, R(*handler)(A1))
        {
            add_handler(method, std::function<R(A1)>(handler));
        }
    // 2
    template<typename R, typename A1, typename A2>
        void add_handler(const std::string &method, R(*handler)(A1, A2))
        {
            add_handler(method, std::function<R(A1, A2)>(handler));
        }
    // 3
    template<typename R, typename A1, typename A2, typename A3>
        void add_handler(const std::string &method, R(*handler)(A1, A2, A3))
        {
            add_handler(method, std::function<R(A1, A2, A3)>(handler));
        }
    // 4
    template<typename R, typename A1, typename A2, typename A3, typename A4>
        void add_handler(const std::string &method, R(*handler)(A1, A2, A3, A4))
        {
            add_handler(method, std::function<R(A1, A2, A3, A4)>(handler));
        }

    // for std::bind
    // 0
    template<typename R, typename C>
        void add_bind(const std::string &method, R(C::*handler)(), 
                C *self)
        {
            add_handler(method, std::function<R()>(std::bind(handler, self)));
        }

    // 1
    template<typename R, typename C, typename A1, 
        typename B1>
        void add_bind(const std::string &method, R(C::*handler)(A1), 
                C *self, B1 b1)
        {
            add_handler(method, std::function<R(A1)>(std::bind(handler, self, b1)));
        }

    // 2
    template<typename R, typename C, typename A1, typename A2, 
        typename B1, typename B2>
        void add_bind(const std::string &method, R(C::*handler)(A1, A2), 
                C *self, B1 b1, B2 b2)
        {
            add_handler(method, std::function<R(A1, A2)>(
                        std::bind(handler, self, b1, b2)));
        }

    // 3
    template<typename R, typename C, typename A1, typename A2, typename A3, 
        typename B1, typename B2, typename B3>
        void add_bind(const std::string &method, R(C::*handler)(A1, A2, A3), 
                C *self, B1 b1, B2 b2, B3 b3)
        {
            add_handler(method, std::function<R(A1, A2, A3)>(
                        std::bind(handler, self, b1, b2, b3)));
        }

    // 4
    template<typename R, typename C, typename A1, typename A2, typename A3, typename A4,
        typename B1, typename B2, typename B3, typename B4>
        void add_bind(const std::string &method, R(C::*handler)(A1, A2, A3, A4), 
                C *self, B1 b1, B2 b2, B3 b3, B4 b4)
        {
            add_handler(method, std::function<R(A1, A2, A3, A4)>(
                        std::bind(handler, self, b1, b2, b3, b4)));
        }

    // for std::bind(void)
    // 4
    template<typename C, typename A1, typename A2, typename A3, typename A4,
        typename B1, typename B2, typename B3, typename B4>
        void add_bind(const std::string &method, void(C::*handler)(A1, A2, A3, A4), 
                C *self, B1 b1, B2 b2, B3 b3, B4 b4)
        {
            add_handler(method, std::function<void(A1, A2, A3, A4)>(
                        std::bind(handler, self, b1, b2, b3, b4)));
        }

    // for std::bind(const)
    // 0
    template<typename R, typename C>
        void add_bind(const std::string &method, R(C::*handler)()const, 
                C *self)
        {
            add_handler(method, std::function<R()>(std::bind(handler, self)));
        }

    // 1
    template<typename R, typename C, typename A1, typename B1>
        void add_bind(const std::string &method, R(C::*handler)(A1)const, 
                C *self, B1 b1)
        {
            add_handler(method, std::function<R(A1)>(std::bind(handler, self, b1)));
        }

    // 2
    template<typename R, typename C, typename A1, typename A2, typename B1, typename B2>
        void add_bind(const std::string &method, R(C::*handler)(A1, A2)const, 
                C *self, B1 b1, B2 b2)
        {
            add_handler(method, std::function<R(A1, A2)>(std::bind(handler, self, b1, b2)));
        }

    // utility
    template<typename T, typename V>
        void add_property(const std::string &property,
                std::function<T*()> thisGetter,
                V(T::*getMethod)()const,
                void(T::*setMethod)(V)
                )
        {
            add_handler(std::string("get_")+property, [thisGetter, getMethod](
                        )->V{
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    return (self->*getMethod)();
                    });
            add_handler(std::string("set_")+property, [thisGetter, setMethod](
                        const V& value){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*setMethod)(value);
                    });
        }
    // utility(const &)
    template<typename T, typename V>
        void add_property(const std::string &property,
                std::function<T*()> thisGetter,
                V(T::*getMethod)()const,
                void(T::*setMethod)(const V&)
                )
        {
            add_handler(std::string("get_")+property, [thisGetter, getMethod](
                        )->V{
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    return (self->*getMethod)();
                    });
            add_handler(std::string("set_")+property, [thisGetter, setMethod](
                        const V& value){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*setMethod)(value);
                    });
        }


    // ToDo
    // removeMethod
    template<typename T, typename V>
        void add_list_property(const std::string &property,
                std::function<T*()> thisGetter,
                void(T::*clearMethod)(),
                void(T::*addMethod)(const V&),
                //void(T::*updateMethod)(const V&),
                void(T::*updateAtMethod)(size_t, const V&),
                void(T::*removeAtMethod)(size_t),
                void(T::*movefromtoMethod)(size_t, size_t),
                std::list<V>(T::*listMethod)()const              
                )
        {
            if(clearMethod){
            add_handler(std::string("clear_")+property, [thisGetter, clearMethod](
                        ){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*clearMethod)();
                    });
            }
            if(addMethod){
            add_handler(std::string("additem_")+property, [thisGetter, addMethod](
                        const V &item){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*addMethod)(item);
                    });
            }
            /*
            if(updateMethod){
            add_handler(std::string("updateitem_")+property, [thisGetter, updateMethod](
                        const V &item){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*updateMethod)(item);
                    });
            }
            */
            if(updateAtMethod){
            add_handler(std::string("updateitemat_")+property, [thisGetter, updateAtMethod](
                        size_t index, const V &item){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*updateAtMethod)(index, item);
                    });
            }
            if(removeAtMethod){
            add_handler(std::string("removeat_")+property, [thisGetter, removeAtMethod](
                        size_t index){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*removeAtMethod)(index);
                    });
            }
            if(movefromtoMethod){
            add_handler(std::string("movefromto_")+property, [thisGetter, movefromtoMethod](
                        size_t from, size_t to){
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    (self->*movefromtoMethod)(from, to);
                    });
            }
            if(listMethod){
            add_handler(std::string("list_")+property, [thisGetter, listMethod](
                        )->std::list<V>{
                    auto self=thisGetter();
                    if(!self){
                    throw msgerror("fail to convert args", error_self_pointer_is_null);
                    }
                    return (self->*listMethod)();
                    });
            }
        }

};
