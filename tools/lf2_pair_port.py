"""Index all enhanced Ghidra functions against the current port.

Address citations are navigational evidence, NOT an equivalence proof. Manual
assessments distinguish reviewed subsets from unpaired functions. This tool
never edits recovered C, original exports, or source code.
"""
from pathlib import Path
import bisect
import collections
import hashlib
import json
import re

ROOT=Path(__file__).resolve().parents[1]
AUDIT=ROOT/"reference/audit-ghidra-2026-09-20/enhanced"
OUT=ROOT/"reference/round-pairing-2026-09-21"

# Reviewed correspondences. A destination is a subsystem/callsite, not a claim
# that the port reproduces the whole original function.
CORE={
 "004061d0": ("Inicializacao do objeto", ["src/engine/fighter.hpp"],
     "MP inicial pareado nesta rodada; layout, HP recuperavel e demais campos apenas parciais."),
 "0040bfb0": ("Busca de imagem de um frame", ["src/engine/fighter.hpp","src/main.cpp"],
     "Compara pic a faixas de folhas. Comentario antigo do parser citava 40bff0 incorretamente como loader de background; corrigido."),
 "0040c160": ("Parser de background", ["src/engine/dat.hpp"],
     "loadBackground: reconhece layer/rect/height/transparency/shadow/perspective; equivalencia integral do parser nao demonstrada."),
 "0040ef70": ("Parser de dados de objeto/frame", ["src/engine/dat.hpp"],
     "parse: header, frames, itr/bdy/wpoint/cpoint/opoint. Campos aceitos pelo parser nao significam mecanicas implementadas."),
 "004122f0": ("Indice de objetos e backgrounds", ["src/engine/dat.hpp","src/main.cpp"],
     "parseIndex/loadIndex/objAssets: <object>/<background>, id/type/file. Tipos e identidade utilizados por MP e spawn."),
 "004146b0": ("Codificacao DAT", ["src/engine/dat.hpp"],
     "encrypt e ferramenta de testes, nao replica o cabecalho aleatorio nem o I/O texto Win32; nao e equivalente completo."),
 "004148a0": ("Decodificacao DAT para arquivo temporario", ["src/engine/dat.hpp"],
     "decrypt/loadText: nucleo byte a byte validado em 9472 casos emulados. Port usa memoria/I/O binario; I/O Win32 nao reproduzido."),
 "00414a30": ("Decodificacao DAT para destino explicito", ["src/engine/dat.hpp"],
     "Mesmo papel/algoritmo no pseudocodigo; oraculo executa a variante 4148a0, nao esta rotina inteira."),
 "0040d960": ("Manutencao/clock/next", ["src/engine/fighter.hpp","src/characters/player.hpp","src/engine/object.hpp"],
     "M1 parcial; clock de opoint e contador assinado +8 conferidos nesta rodada. Gates cpoint completos pendentes."),
 "0040e490": ("Movimento, shaking e contato", ["src/characters/player.hpp","src/engine/object.hpp"],
     "M1 cobre tombo comum/type0; nao fecha todos os estados, armas, efeitos ou precisao x87."),
 "00417170": ("Gerador RNG", ["src/engine/random.hpp"],
     "Traducao existente e testada; equivalencia dos sitios/ordem de consumo no jogo inteiro pendente."),
 "00417f80": ("Limites de vitais", ["src/characters/player.hpp","src/engine/fighter.hpp"],
     "HP recuperavel (+300) ainda nao representado; nao confundir com MP (+308)."),
 "004196f0": ("Commit dos acumuladores de impulso", ["src/engine/hit_push.hpp","src/characters/player.hpp","src/engine/object.hpp"],
     "Correspondencia M1 existente; preservada e exercitada na suite de regressao."),
 "0041a250": ("Desenho de background", ["src/main.cpp","src/engine/dat.hpp"],
     "Scene/renderBackground/layerScreenX: geometria parcial; viewport e preenchimento sao adaptacoes Vita."),
 "0041bc90": ("Simulacao central", ["src/main.cpp","src/characters/player.hpp","src/engine/vitals.hpp","src/engine/opoint.hpp"],
     "MP, count/facing/leque, arest inicial e gate do clock pareados nesta rodada. Colisoes, spawns e modos restantes parciais."),
 "00422ac0": ("Geracao da tabela RNG", ["src/engine/random.hpp"],
     "Traducao existente; seed/relógio de host adaptados, sem declarar replay identico do Windows."),
 "004246b0": ("Controle exterior/modos e chamada do tick", ["src/main.cpp"],
     "Menu/reset/loop SDL sao parciais; port nao implementa todos os modos do executavel."),
 "0042e100": ("Resolucao de impactos", ["src/main.cpp","src/characters/player.hpp","src/engine/object.hpp","src/engine/hit_push.hpp"],
     "M1/A8-A17 parciais; efeitos especiais, captura/cpoint e todos os guards nao estao encerrados."),
}
FILES={
 "src/engine/frontend.hpp":"Fluxo de menus adaptado para Vita; modos ausentes explicitamente indisponiveis. Ver auditoria de interface 2026-09-22.",
 "src/engine/frontend_render.hpp":"Recursos RT_BITMAP originais e fonte de 4246b0; layout/controles adaptados. Nao equivale a todos os modos originais.",
 "src/main.cpp":"Loop, colisao, spawn, roster, HUD e SDL. 41bc90 e 42e100 sao distribuídas entre main e headers, sem equivalencia integral.",
 "src/engine/dat.hpp":"Parsing/decodificacao/indice; 40c160, 40ef70, 4122f0, 4148a0. Decodificacao byte a byte confirmada; parser e consumidores parciais.",
 "src/engine/fighter.hpp":"Frames, boxes, next/wait, vitais e shaking. Clock/estado parcial de 40d960; inicializacao MP de 4061d0.",
 "src/characters/player.hpp":"Input, movimento, reacao e manutencao do ator. Varios ramos de 40e490/41bc90/42e100; ainda contem inferencias marcadas.",
 "src/characters/enemy.hpp":"Reutiliza Player, inclusive MP e hitstop. IA de perseguicao/xorshift do port nao e pareamento da IA original.",
 "src/engine/object.hpp":"Armas/projeteis, manutencao e transporte de impulso parciais. TTL/pool/lifecycle sao politicas do port; spawn de personagem nao implementado.",
 "src/engine/hit_push.hpp":"Aritmetica dos acumuladores de impacto; consumidores em 42e100 e commit em 4196f0. Cobertura M1 preservada.",
 "src/engine/random.hpp":"417170 e 422ac0. Nucleo RNG traduzido; todos os consumidores e ordem de sorteio ainda nao pareados.",
 "src/engine/opoint.hpp":"NOVO: formulas de count/facing/leque e arest inicial de 41bc90, com 480+79 vetores derivados das instrucoes x86.",
 "src/engine/vitals.hpp":"NOVO: MP inicial e regeneracao de 41bc90, com 12723 vetores derivados das instrucoes x86.",
 "src/engine/types.hpp":"Constantes/controles/viewport do port. Gravidades auditadas; outras convencoes ainda nao provadas globalmente.",
 "src/engine/render.hpp":"Adaptacao para SDL2/PNG em lugar de DirectDraw/BMP; sem traducao um-para-um do backend Win32.",
 "src/engine/log.hpp":"Diagnostico do port; nao representa uma rotina de gameplay extraida.",
}
NEW={
 "0043b3d0": ("plataforma", "WndProc: SDL_PollEvent/readInput/render em main.cpp substituem parte das responsabilidades; mensagens Win32 nao sao mecanicas novas."),
 "0043c240": ("fora_do_escopo", "Thread de anuncios: data/ad*.txt e sprite/sys/ad*.bmp; sem consumidor no port."),
 "0043fb70": ("suporte_compressao", "Callback de compressao, referenciado por tabela .rdata; nenhum equivalente de gameplay demonstrado."),
 "0043fcb0": ("suporte_compressao", "Callback de compressao com busca por sequencias; nenhum equivalente de gameplay demonstrado."),
 "0043ffb0": ("suporte_compressao", "Callback de compressao com buffers/tabelas; nenhum equivalente de gameplay demonstrado."),
 "00442750": ("runtime", "Callback free(segundo argumento); alocacao/liberacao pertence ao runtime C/C++ do destino."),
 "004451c2": ("runtime", "Wrapper except_handler4_common/security cookie."),
 "00445254": ("runtime", "Inicializacao CRT: __getmainargs e tratamento de falha."),
 "0044547e": ("runtime", "Inicializacao CRT: __set_app_type, fmode/commode e ponteiros codificados."),
 "0044571e": ("runtime", "Inicializacao de informacao SSE2 (__get_sse2_info)."),
 "0044576f": ("runtime", "Instalacao de SetUnhandledExceptionFilter."),
 "0044585f": ("runtime", "Callback vazio registrado pelo startup CRT."),
 "00445b1c": ("runtime", "Consulta GetThreadLocale/GetLocaleInfoA; suporte de runtime/locale."),
 "00445b81": ("runtime", "Callback retorna 3, referenciado pelo mesmo conjunto de suporte de locale; sem regra de jogo demonstrada."),
 "004462e0": ("global_a_investigar", "Zera 458440/458570/458574. Semantica das tres globais nao reconstruida; nao copiar por suposicao."),
 "004462f0": ("global_a_investigar", "Retorna endereco 458af8. Contexto de tabela de inicializacao; sem consumidor equivalente provado."),
 "00446300": ("global_a_investigar", "Inicializa 458b00 via 419e40 (zero + memset de 400 bytes). Layout sem equivalente provado."),
}
SEH={"00445c5b","00445c8e","00445d30","00445da6","00445dee","00445eba",
     "00445efb","00446080","0044614a","0044619c","004461db","0044620c",
     "0044624c","004462a8"}
for addr in SEH:
    NEW[addr]=("runtime", "Handler de excecao/C++: security cookie e __CxxFrameHandler3; gerado pelo compilador, nao portar como rotina de jogo.")


def main():
    exports={p.stem:json.loads(p.read_text(encoding="utf-8"))
             for p in sorted((AUDIT/"exports").glob("00*.json"))}
    assert len(exports)==403
    recovered={x["address"] for x in json.loads((AUDIT/"recovery_decisions.json").read_text())
               if x["action"]=="created"}
    assert recovered==set(NEW) and len(recovered)==31
    assert set(CORE)<=set(exports)
    ranges=sorted((int(lo,16),int(hi,16),addr) for addr,d in exports.items()
                  for lo,hi in d["body_ranges"])
    starts=[x[0] for x in ranges]
    references=collections.defaultdict(list)
    pattern=re.compile(r"(?:0x|FUN_|(?<![0-9A-Za-z_]))(00[4-7][0-9a-fA-F]{5}|[4-7][0-9a-fA-F]{5})(?![0-9a-fA-F])")
    source_hashes={}
    for path in sorted((ROOT/"src").rglob("*")):
        if not path.is_file(): continue
        rel=path.relative_to(ROOT).as_posix()
        source_hashes[rel]=hashlib.sha256(path.read_bytes()).hexdigest()
        for line,text in enumerate(path.read_text(encoding="utf-8").splitlines(),1):
            for match in pattern.finditer(text):
                va=int(match[1],16); idx=bisect.bisect_right(starts,va)-1
                if idx>=0 and va<=ranges[idx][1]:
                    references[ranges[idx][2]].append({"path":rel,"line":line,"address":f"{va:08x}"})
    rows=[]
    for addr,d in exports.items():
        core=CORE.get(addr)
        cls,note=NEW.get(addr,("sem_pareamento_revisado","Disponivel para analise; ausencia de citacao nao prova ausencia de implementacao."))
        targets=[]
        if core:
            cls="pareamento_parcial_revisado";title,targets,note=core
        else:
            title=d["name"]
            if addr not in NEW and references[addr]: cls="citacao_existente_nao_revalidada"
        refs=references[addr]
        assert all((ROOT/t).is_file() for t in targets)
        rows.append({"address":addr,"name":d["name"],"role":title,
                     "status":cls,"assessment":note,"new_in_export":addr in recovered,
                     "body_bytes":sum(int(b,16)-int(a,16)+1 for a,b in d["body_ranges"]),
                     "port_files":targets,"source_citations":refs})
    counts=dict(collections.Counter(r["status"] for r in rows))
    result={"function_count":len(rows),"new_function_count":len(recovered),
            "status_counts":counts,"source_sha256":source_hashes,"functions":rows,
            "file_assessments":FILES,
            "warning":"Index/citations are not proof of full behavioral equivalence."}
    assert set(FILES)==set(source_hashes), "update file-level map for new source files"
    (OUT/"port-map.json").write_text(json.dumps(result,indent=2,ensure_ascii=False)+"\n",encoding="utf-8")
    md=["# Pareamento Ghidra → port C++", "",
        "403 funções catalogadas; 31 entradas recuperadas classificadas individualmente.",
        "As correspondências são parciais. Citação de endereço em comentário serve para navegação; não prova equivalência.",
        "Código do jogo continua em `src/`. Este índice não substitui C++ por JSON.", "",
        "## Por arquivo do port", "", "| Arquivo | Correspondência e limite |", "|---|---|"]
    md += [f"| [{f}](../../{f}) | {note} |" for f,note in FILES.items()]
    md += ["",
        "## Núcleo revisado", "", "| Função | Responsabilidade | Arquivos do port | Limite |",
        "|---|---|---|---|"]
    for r in rows:
        if r["address"] in CORE:
            files="<br>".join(f"[{f}](../../{f})" for f in r["port_files"])
            md.append(f"| `{r['address']}` | {r['role']} | {files} | {r['assessment']} |")
    md += ["", "## Todas as 31 entradas recuperadas", "",
           "| Entrada | Classe | Decisão de pareamento |", "|---|---|---|"]
    for r in rows:
        if r["new_in_export"]:
            md.append(f"| `{r['address']}` | {r['status']} | {r['assessment']} |")
    md += ["", "## Catálogo completo", "",
           "Endereços abaixo abrem a referência exportada. A evidência em C está em `../audit-ghidra-2026-09-20/enhanced/lf2_recovered.c`.",
           "", "| Entrada | Nome no Ghidra | Estado | Arquivos que citam seu corpo |", "|---|---|---|---|"]
    for r in rows:
        files=sorted({x["path"] for x in r["source_citations"]})
        links=", ".join(f"[{f}](../../{f})" for f in files) or "—"
        addr=r["address"]
        md.append(f"| [{addr}](../audit-ghidra-2026-09-20/enhanced/exports/{addr}.json) | `{r['name']}` | {r['status']} | {links} |")
    (OUT/"mapa-funcoes.md").write_text("\n".join(md)+"\n",encoding="utf-8")
    print(json.dumps({"functions":len(rows),"new":len(recovered),"status_counts":counts},indent=2))


if __name__=="__main__": main()
